/*
 * Main for ui compiler for QSkinny.
*/
#include <QtCore/QCoreApplication>
#include <QtCore/QCommandLineParser>
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtCore/QString>
#include <QStack>
#include <QtCore/QXmlStreamReader>
#include <qskinny/QskGlobal.h>
#include <iostream>
#include <filesystem>

/*
 * If version number is not given from the compiler then default to "git",
*/
#ifndef PROGNAME
#define PROGNAME qskuic
#endif
#define PROGNAME_STR QT_STRINGIFY(PROGNAME)

/*
 * If version number is not given from the compiler then default to "git",
*/
#ifndef VERSION
#define VERSION git
#endif
#define VERSION_STR QT_STRINGIFY(VERSION)

/*
 * Types of value types. Each of these has it's own tag.
*/
enum ValueType {
	OBJECT,
	NUMBER,
	QSTRING,
	CSTRING,
	SSTRING,
	BOOL,
	RECT,
};

// See below this is just forward dec.
struct ValueObjectType;
struct ValueString;
struct ValueRect;

/*
 * Types of structures parameter list may point to.
*/
union ParameterListTypes {
	void *shadow;
	ValueType *type;
	ValueString *str;
	ValueObjectType *obj;
	ValueRect *rect;
	
	ParameterListTypes(ValueString *str){
		this->str = str;
	}

	ParameterListTypes(ValueObjectType *obj){
		this->obj = obj;
	}

	ParameterListTypes(ValueRect *rect){
		this->rect = rect;
	}
};

/*
 * Alias QList for the parametrers list.
*/
typedef QList<union ParameterListTypes> ParameterList;

/*
 * For simple value type structure stored is just QString since we just pasting it to the output.
*/
typedef struct ValueString {
	// Type of value this is.
	ValueType type;
	// Value for the type.
	QString value;

	ValueString(ValueType type, QString value) {
		this->type = type;
		this->value = value;
	}
} ValueString;

/*
 * Object valie type is special as the it has it's own ParameterList for the constructor.
*/
typedef struct ValueObjectType {
	// Type of the value. Must be `ValueType::OBJECT`.
	ValueType type;
	// Name of the object.
	QString objname;
	// List of parameters to the constructor of the object.
	ParameterList parameters;

	/*
	* Since there is only one type force it by constructor. Also for object name.
	*/
	ValueObjectType(QString objname) {
		this->type = ValueType::OBJECT;
		this->objname = objname;
		this->parameters = ParameterList();
	}

} ValueObjectType;

/*
 * Special type for the QRect. QRect is not used since QString is used in generation anyway.
*/
typedef struct ValueRect {
	// Type must be QRect.
	ValueType type;
	// Corresponce to QRect::x
	QString x;
	// Corresponce to QRect::y
	QString y;
	// Corresponce to QRect::width
	QString width;
	// Corresponce to QRect::height
	QString height;

} ValueObjectRect;

/*
 * Structure for the controls property.
*/
typedef struct ControlProperty {
	// Name of the property.
	QString property;
	// List of the parameters for the set method.
	ParameterList parameters;
} ControlProperty;

/*
 * Control node is the n-tree node representing a control. Tree is generated parent to child. Parent only connections
 * to one sibling which connections to other childer. This done because easy dynamic response and we most likelly
 * have to multiple passes of generation.
*/
typedef struct ControlNode {
	// Pointer to sibling of this control.
	struct ControlNode *sibling;
	// Pointer to to last child control of this control.
	// USE ADDING NOT FOR PROCESSING!
	struct ControlNode *lastchild;
	// Name of the variable in the structure.
	QString variable_name;
	// Pointer to first child of this control which through you get the other childern.
	struct ControlNode *firstchild;
	// Pointer to the parent control.
	struct ControlNode *parent;
	// Name of the control.
	QString control_name;
	// Properties to be set during the setup call.
	QList<ControlProperty*> properties;
	// Flag zone for the control to save memory on bools.
	struct {
		// Indicates was the control prefixed with "c:" or not when parsed.
		// TODO: Is there a way get rid of this....
		bool prefixed:1;
	} flags;

	~ControlNode(){
		// We need to deallocate the properties.
		// We do not deallocate ConrolNode pointers to keep a processing not hit free after use.
		for(auto prop : this->properties) {
			delete prop;
		}
	}

} ControlNode;

/*
 * Sepcial variable to indicate empty string. Used as intialization for some QStrings.
*/
static const QString Empty = QStringLiteral("");

/*
 * List of tag name in constant strings to avoid writting errors when used multiple times.
*/
static const QString TagUi = QStringLiteral("ui");
static const QString TagExportMacro = QStringLiteral("exportmacro");
static const QString TagAuthor = QStringLiteral("author");
static const QString TagDescription = QStringLiteral("description");
static const QString TagInclude = QStringLiteral("include");
static const QString TagStruct = QStringLiteral("struct");
static QString TagObject = QStringLiteral("object");

/*
 * Just string for the "->" which is used in C/C++ reference member of pointer.
*/
static const QString RefPointer = QStringLiteral("->");

/*
 * Just a zero for returning a zero from special case parsing.
*/
static const QString Zero = QStringLiteral("0");

/*
 * Parse a internals of pure text tag. Needs tag name for the error messages.
*/
static QString parse_text_tag(QXmlStreamReader *reader, QString nodename) {
	auto nodeid = reader->readNext();
	if(nodeid == QXmlStreamReader::TokenType::Characters) {
		
		// Before returning the check that end tag is reached for given node.
		// Return value is cached for time being. Note also that QStringView returned by reader
		// would change before is is used so we need to change it to solid memory.
		QString returner = reader->text().toString();

		reader->readNext();
		if(reader->isEndElement()) {
			if (reader->name() == nodename) return returner;
			else{
				std::cerr << "End tag found but it was not for " << nodename.toStdString() << ".\n";
				exit(1);
			}
		}
		else{
			std::cerr << "Inside tag " << nodename.toStdString() << " has to be just text.\n";
			exit(1);
		}
	}
	else {
		std::cerr << "Inside " << nodename.toStdString() << " tag must be text." << '\n';
		exit(1);
	}
}

/*
 * If tag only accepts an integer child this function can be used to parse the result.
*/
static QString parse_integer_tag(QXmlStreamReader *reader, QString nodename, bool &ok) {
	QString text = parse_text_tag(reader, nodename);

	// If we get empty text we interpert it as zero.
	if(text.isEmpty()) {
		ok = true;
		return Zero;
	}

	// Check if the text is a valid integer representation
	int i = 0;

	// Check for optional sign
	if(text[0] == '+' || text[0] == '-') {
		i = 1;
		// If we only have sign we do not consider string valid number.
		if(text.length() == 1) {
			ok = false;
			return Empty;
		}
	}

	// Check remaining characters are digits
	for(; i < text.length(); i++) {
		if(!text[i].isDigit()) {
			ok = false;
			return Empty;
		}
	}

	ok = true;
	return text;
}

/*
 * Parse a floating point from the text string.
*/
static QString parse_float_tag(QXmlStreamReader *reader, QString nodename, bool &ok) {
	QString text = parse_text_tag(reader, nodename);

	// Check if the text is a valid float representation
	int i = 0;

	// Special handle case where string is empty. This is interperted as zero.
	if(text.isEmpty()){
		ok = true;
		return Zero;
	}

	// Check for optional sign
	if(text[0] == '+' || text[0] == '-') {
		i = 1;
		// If only sign is given we consider that invalid.
		if(text.length() == 1) {
			ok = false;
			return Empty;
		}
	}

	for(; i < text.length(); i++) {
		if(text[i] == '.') {
			// We have found a digit so now we can speed up dectection by
			// just checking everything is a digit.
			i++;
			for(; i < text.length(); i++) {
				if(!text[i].isDigit()) {
					ok = false;
					return Empty;
				}
			}

			break;
		}
		else if (!text[i].isDigit()){
			ok = false;
			return Empty;
		}
	}

	ok = true;
	return text;
}

/*
* Moves reader to next token and returns is document end reached or end element not reached.
* Moves over any whitespace characters and comments.
*/
static inline bool was_moved_to_next_token_in_element(QXmlStreamReader *reader, QXmlStreamReader::TokenType &token){
	bool ok;
	do{
		token = reader->readNext();
	}while(((token == QXmlStreamReader::Characters && reader->isWhitespace()) || token == QXmlStreamReader::Comment) && !reader->atEnd());
	return !reader->atEnd() && token != QXmlStreamReader::TokenType::EndElement;
}

/*
* Parses the property tag that was hit during processing. Property name, tag of that property can be different.
* For example: Tag "p.Text" will have propname is "Text" but tagname is still "p.Text".
*/
static ControlProperty *parse_property_tag(QXmlStreamReader *reader, QString tagname, QString propname) {
	// Support macro for the repeative task of freeing allocated memory in-case of
	// parsing error.
	#define RETURN_ERROR \
	{                    \
		delete prop;       \
		return nullptr;    \
	}                    \

	// Allocate the new property first so it is clear where parameters are stored and at error we have to free
	// what we allocated.
	auto prop = new ControlProperty;
	prop->property = propname;
	prop->parameters = ParameterList();

	// Since object tag takes in same tags as properties use stack to push what list is been updated.
	typedef QPair<ParameterList&, QString&> StackSlot;
	QStack<StackSlot> stack;
	stack.push(QPair<ParameterList&, QString&>(prop->parameters, tagname));

	QXmlStreamReader::TokenType token;
	do{
		while(was_moved_to_next_token_in_element(reader, token)) {

			switch(token) {
				case QXmlStreamReader::TokenType::StartElement:
				{
					QStringView name = reader->name();
					if(name == TagObject){

						// TODO: Parse the attribute for the name.
						QString nameobj = "foo";

						// Allocate an object and place push to a stack next new parameter list.
						auto object = new ValueObjectType(nameobj);
						ParameterList& list = stack.top().first;
						list.append(object);

						stack.push(StackSlot(object->parameters, TagObject));
					}
					else if(name == "number"){
						bool ok;
						QString number = parse_float_tag(reader, "number", ok);
						if (!ok) return nullptr;
						stack.top().first.append(new ValueString(ValueType::NUMBER, number));
					}
					else if(name == "qstring"){
						stack.top().first.append(new ValueString(ValueType::QSTRING, parse_text_tag(reader, "qstring")));
					}
					else if(name == "cstring"){
						stack.top().first.append(new ValueString(ValueType::CSTRING, parse_text_tag(reader, "cstring")));
					}
					else if(name == "sstring"){
						stack.top().first.append(new ValueString(ValueType::SSTRING, parse_text_tag(reader, "sstring")));
					}
					else if (name == "bool") {
						QString boolean = parse_text_tag(reader, "bool");
						// Check that we have ether true or false or any upper case variant.
						//
						// NOTE: Qt's `QString::toLower()` makes a copy so we are avoiding that here.
						int len = boolean.length();
						if (len == 4){
							// Is it true?
							for(int i = 0; i < 4; i++) {
								boolean[i] = boolean[i].toLower();
							}
							if (boolean != "true"){
								// TODO: Better message.
								std::cerr << "Syntax error!\n";
								RETURN_ERROR
							}
						}
						else if (len == 5){
							// Is is false
							for(int i = 0; i < 5; i++) {
								boolean[i] = boolean[i].toLower();
							}
							if (boolean != "false"){
								// TODO: Better message.
								std::cerr << "Syntax error!\n";
								RETURN_ERROR
							}
						}
						// TODO: Better message.
						else std::cerr << "Syntax error!\n";

						stack.top().first.append(new ValueString(ValueType::BOOL, boolean));
					}
					else if (name == "rect") {

						// Allocate the rectangle and then use try fill it in.
						auto rect = new ValueRect();
						rect->type = ValueType::RECT;

						while ((token = reader->readNext()) && token != QXmlStreamReader::TokenType::EndElement && !reader->atEnd()){
							switch(token) {
								case QXmlStreamReader::TokenType::StartElement:
									{
										bool ok;
										name = reader->name();

										QString value = parse_integer_tag(reader, QString(name), ok);
										if(name == "x") rect->x = value;
										else if(name == "y") rect->y = value;
										else if(name == "width") rect->width = value;
										else if(name == "height") rect->height = value;

										stack.top().first.append(rect);
									}
									break;
								}
							}
							stack.top().first.append(rect);
					}
					else {
						std::cerr << "Unknown tag under "<< tagname.toStdString() << " tag. Tag found was \"" << name.toString().toStdString() << "\".\n";
						RETURN_ERROR
					}
					break;
				}
				case QXmlStreamReader::TokenType::Characters:
					if (reader->isWhitespace()){
						continue;
					}
					std::cerr << "Tag " << tagname.toStdString() << " does not support general text.\n";
					RETURN_ERROR
			}
		}
		
		// Checking that property tag was ended.
		if(reader->name() != stack.top().second) {
			std::string str = stack.top().second.toStdString();
			std::cerr << "Tag "<< str << " was not closed. Check tag endings inside of " << str << " if ending tag exists.\n";
			RETURN_ERROR
		}
		stack.pop();
	} while(!stack.isEmpty());

	return prop;
	#undef RETURN_ERROR
}

/*
 * Handles p.<property> tag when caller knows that is the tag. Done mostly to get same error message to multiple place in code.
*/
static ControlProperty *parse_pdotproperty_tag(QXmlStreamReader *reader, QString tagname){
	if (tagname.length() <= 2) {
		std::cerr << "Syntax error. No property name was given after \"p.\" in property tag.\n";
		exit(1);
	}
	QString propname = tagname.sliced(2);
	return parse_property_tag(reader, tagname, propname);
}

/*
 * Allocate new control node with default initialization on some members and others given as parameters.
*/
static ControlNode* allocate_control_node(QString cntrlname, QString varname, ControlNode *parent, bool prefixed) {
	ControlNode *newnode = new ControlNode;
	newnode->sibling = nullptr;
	newnode->firstchild = nullptr;
	// NOTE: Pointer trick to avoid checking does firstchild exist or not.
	//       Works only if sibling is the first pointer the list.
	newnode->lastchild = (ControlNode*)&newnode->firstchild;
	newnode->parent = parent;
	newnode->control_name = cntrlname;
	newnode->variable_name = varname;
	newnode->flags.prefixed = prefixed;
	return newnode;
}

/*
 * Add given string to the stream by adding quotes around it.
*/
static void generate_controls_setup_string_helper(QTextStream &stream, QString& value){
	stream << "\"" << value << "\"";
}

/*
 * Generate the setup commands for given control.
*/
static void generate_controls_setup(QTextStream &stream, ControlNode *node) {
	// Generate the allocation command.
	// NOTE: We do not need to deacllocate long as QQuick scene had pointer to this.
	stream << "\t\t" << node->variable_name << " = new " << node->control_name << "(" << node->parent->variable_name << ");\n";

	// Set the variable name as the object name for easier to find the control.
	stream << "\t\t" << node->variable_name << RefPointer << "setObjectName(QString(\""<< node->variable_name << "\"));\n";

	// Generate property setters.
	for(auto &property : node->properties) {
		QString propname = property->property;
		
		stream << "\t\t" << node->variable_name << RefPointer << "set" << propname << "(";

		// How paramter is given out depends upon it's type.
		ParameterListTypes types = property->parameters.at(0);
		switch (*types.type){
			case ValueType::OBJECT:
				//TODO: Handle internal parameter list.
				stream << types.obj->objname << "(";
				break;
			case ValueType::RECT:
				stream << "QRect(" << types.rect->x << types.rect->y << types.rect->width << types.rect->height << ")";
				break;
			case ValueType::NUMBER:
			case ValueType::BOOL:
				// We have previously already handled the number and bool to be
				// correct format so we can just push them to the stream.
				stream << types.str->value;
				break;
			case ValueType::CSTRING:
				// Regular char string we just have to add quotes around value.
				generate_controls_setup_string_helper(stream, types.str->value);
				break;
			case ValueType::QSTRING:
				stream << "QString(";;
				generate_controls_setup_string_helper(stream, types.str->value);
				stream << ")";
				break;
			case ValueType::SSTRING:
				stream << "std::String(";
				generate_controls_setup_string_helper(stream, types.str->value);
				stream << ")";
				break;
		}
		stream << ");\n";

	}

	// Have some space at the end for confurtable human review.
	stream << "\n";
}

/*
 * Start of the program. Drives the compilation.
*/
int main(int argn, char **argv) {

	QCoreApplication app(argn, argv);
	QCoreApplication::setApplicationName(PROGNAME_STR);
	QCoreApplication::setApplicationVersion(VERSION_STR);

	QCommandLineParser parser;

	parser.setApplicationDescription("User interface compiler for QSkinny version " QSK_VERSION_STR);
	QCommandLineOption outputoption = {QStringList({QStringLiteral("o"), QStringLiteral("output")}), "Write generated file to location in <file>.", "file"};
	parser.addOption(outputoption);
	parser.addPositionalArgument("qskuicfile", "Source UIC file.", "<qskuic file>");
	parser.addHelpOption();
	parser.addVersionOption();

	parser.process(app);

	const QStringList srcfiles = parser.positionalArguments();
	if(srcfiles.count() == 0) {
		std::cout << "No source files to compile. Exiting..." << std::endl;
		return 0;
	}

	// Allow multiple files to specified but do not advertise.
	// Feature maybe removed.
	const QString srcfile = srcfiles.at(0);
	QFile file(srcfile);
	if(!file.open(QIODevice::ReadOnly)) {
		std::cerr << "Source file not found!" << std::endl;
		return 1;
	}

	QXmlStreamReader xmlreader;
	xmlreader.setDevice(&file);

	QTextStream outputfile;
	if(parser.isSet(outputoption)) {
		// Create the output file and path.
		std::filesystem::path outputfilepath = parser.value(outputoption).toStdString();
		if(std::filesystem::exists(outputfilepath)) {
				// Create the directories up to the file.
				std::filesystem::create_directories(outputfilepath.parent_path());
		}

		// Open the file and then aias to QDataStream for writting.
		auto file = new QFile();
		if(file->open(QIODeviceBase::OpenMode::enum_type::Truncate | QIODeviceBase::OpenMode::enum_type::WriteOnly) == false) {
				std::cerr << "Could not open the output file!" << '\n';
				return 1;
		}

		outputfile.setDevice(file);
	}
	else{
		// Default to standard output.
		new(&outputfile) QTextStream(stdout, QIODeviceBase::OpenMode::enum_type::WriteOnly);
	}

	{
		// String to store information used in overviwer comment.
		QString description = Empty;
		QString author = Empty;

		// Macro added class to control export in some languages.
		QString exportmacro = Empty;

		// List to store includes.
		QList<QString> systemincludes;
		QList<QString> localincludes;

		// There is start document node which we do not need so take it out.
		xmlreader.readNext();

		QXmlStreamReader::TokenType token;
		while((token = xmlreader.readNext()) != QXmlStreamReader::EndDocument && !xmlreader.hasError()) {

			if(token == QXmlStreamReader::StartElement) {
				QStringView name = xmlreader.name();
				if(name == TagUi) {
					// Parse the root "ui" element and its children
					QXmlStreamAttributes attributes = xmlreader.attributes();
					QString version = attributes.value("version").toString();
					if(version.isEmpty()) {
						version = "0.0";
					}

					// Read all children of the "ui" element.
					// If we hit EndElement it must be end of "ui" tag.
					while(was_moved_to_next_token_in_element(&xmlreader, token)) {

						name = xmlreader.name();
						if(name == TagExportMacro) {
							// Do not allow anything else then character node.
							exportmacro = parse_text_tag(&xmlreader, TagExportMacro);
						}else if(name == TagAuthor) {
							// Do not allow anything else then character node.
							author = parse_text_tag(&xmlreader, TagAuthor);
						}else if(name == TagDescription) {
							// Do not allow anything else then character node.
							description = parse_text_tag(&xmlreader, TagDescription);
						}else if(name == TagInclude) {
							// Check the 'location' attribute
							QXmlStreamAttributes attrs = xmlreader.attributes();

							// Assume system include by default.
							enum {LOCAL, SYSTEM} localorsystem = SYSTEM;
							for(int i = 0; i < attrs.size(); i++) {
								QStringView attrName = attrs.at(i).name();
								if (attrName == "location") {
									QString value = attrs.at(i).value().toString();
									if(value == "local") {
										localorsystem = LOCAL;
									}else if (value != "system") {
										std::cerr << "Unknown value for attribute location in " << TagInclude.toStdString() << " tag." << '\n';
										goto jmp_ERROR_CLOSE;
									}
								}else {
									std::cerr << "Unknown attribute in " << TagInclude.toStdString() << " tag." << '\n';
									goto jmp_ERROR_CLOSE;
								}
							}

							// Read the include value
							QString include = QString(parse_text_tag(&xmlreader, TagInclude));

							if(localorsystem == SYSTEM) localincludes.append(include);
							else systemincludes.append(include);

						}else if(name == TagStruct) {

							// Pointer to the first children of the root control.
							// NOTE: Root does not exist as ControlNode.
							struct {
								ControlNode *treeroot;
								ControlNode *lastchild;
								QString setupparm;
							}tree;
							tree.lastchild = (ControlNode*)&tree.treeroot;
							tree.setupparm = "parent";

							// Generate the output comment
							outputfile << QByteArray("/*\nThis file was generated by " PROGNAME_STR ".\n");
							if(description != Empty) outputfile << description.toUtf8() << QByteArray("\n");
							if(author != Empty) outputfile << QByteArray("Author: ") + author.toUtf8() + QByteArray("\n");
							outputfile << QByteArray("*/\n");

							// Output includes
							for(auto include : systemincludes) {
								outputfile << QByteArray("#include <") << include.toUtf8() << ">\n";
							}
							for(auto include : localincludes) {
								outputfile << QByteArray("#include \"") << include.toUtf8() << "\"\n";
							}

							// Extract classname attribute
							QXmlStreamAttributes attrs = xmlreader.attributes();
							QStringView classname;
							for(int i = 0; i < attrs.size(); i++) {
								QStringView attrName = attrs.at(i).name();
								if (attrName == "name") {
									classname = attrs.at(i).value();
								}
							}

							if(classname == Empty) {
								std::cerr << "Syntax error a `class` tag did not have an name attribute. This is mandatory.\n";
								goto jmp_ERROR_CLOSE;
							}

							// Start generating the struct
							outputfile << QByteArray("\nstruct ") << classname << " {\n\n";

							// Processing head for the treeroot.
							// Initialized to the address of treeroot so that first child can be allocated like there is `ControlNode`.
							ControlNode *processinghead = (ControlNode*)&tree;

							while(was_moved_to_next_token_in_element(&xmlreader, token) || xmlreader.name() != TagStruct) {
								switch(token) {
									case QXmlStreamReader::TokenType::Invalid:
										std::cerr << "Invalid token encountered.\n";
										goto jmp_ERROR_CLOSE;
									case QXmlStreamReader::TokenType::StartElement:
										{
											QStringView name = xmlreader.name();
											if (name == "properties") {
												// We expect only properties of the current control here on out. Parse untill end of the tag is reached.
												while(was_moved_to_next_token_in_element(&xmlreader, token)) {
													if(token == QXmlStreamReader::StartElement) {
														QString tagname = QString(xmlreader.name());
														ControlProperty *prop;
														// Here we expect ether p.<property> or <property> tag.
														if(tagname.startsWith(QString("p."))) {
															prop = parse_pdotproperty_tag(&xmlreader, tagname);
														}
														else {
															prop = parse_property_tag(&xmlreader, tagname, tagname);
														}
														if (prop == nullptr) goto jmp_ERROR_CLOSE;

														processinghead->properties.append(prop);
													}
													else {
														std::cerr << "Expected start token for the property tag inside <properties>, got token " << token << '\n';
														goto jmp_ERROR_CLOSE;
													}
												}
											} else if (name.startsWith(QString("p."))) {
												// We have property directly under control without properties tag.

												auto prop = parse_pdotproperty_tag(&xmlreader, QString(name));
												if (prop == nullptr) goto jmp_ERROR_CLOSE;
												processinghead->properties.append(prop);
											}
											else {
												// We assume by default anything under struct or control tag is a other control tag.
												bool prefixed = false;
												if (name.startsWith(QString("c."))){
													if(name.length() == 2){
														std::cerr << "Syntax error. No control class was given after \"c.\".";
														goto jmp_ERROR_CLOSE;
													}
													name = name.sliced(2);
													prefixed = true;
												}

												QStringView varname = QStringView();
												for(auto attribute : xmlreader.attributes()){
													QStringView attrname = attribute.name();
													if (attrname == "name"){
														varname = attribute.value();
													}
													else{
														std::cerr << "Unknown attribute for a control.\n";
														goto jmp_ERROR_CLOSE;
													}
												}

												if (varname.isNull()){
														std::cerr << "Control must have name attribute so that variable can be created.\n";
														goto jmp_ERROR_CLOSE;
												}

												// Create controller node for the tree.
												ControlNode *newnode = allocate_control_node(QString(name), QString(varname), processinghead, prefixed);

												// Move processing head to the new child after we have updated current controls children status.
												processinghead->lastchild->sibling = newnode;
												processinghead->lastchild = newnode;
												processinghead = newnode;

												// Add control to the data structure.
												outputfile << '\t' << name << " *" << varname << ";" << "\n";
											}
										}
										break;
									case QXmlStreamReader::TokenType::EndElement:
										{
											// We need to check that tag ending is the correct one.
											QStringView name = xmlreader.name();
											if (!(processinghead->control_name == name || (processinghead->flags.prefixed && processinghead->control_name == name.sliced(2)))){
												std::cerr << "Tag " << processinghead->control_name.toStdString() << " did not end with correct tag! Ending tag of " << name.toString().toStdString() << " found!\n";
												goto jmp_ERROR_CLOSE;
											}

											// Move to parent as all of the siblings have been processed.
											processinghead = processinghead->parent;

										}
										break;
								}
							}

							// Adding `setup` method.
							// NOTE: We start with newline so that there is padding whatever is above the setup.
							outputfile << "\n\tvoid setup(QskItem *" << tree.setupparm << ") {\n";

							// Second pass to the node tree.
							// We first go to deep and then start processing siblings, if sibling has children process them and go up the tree.
							//
							// Free the memory as you go so that we do not need to have third pass for the memory freeing good points.
							ControlNode *head = tree.treeroot;

							while (head->firstchild != nullptr){
								// Again we should generate what we are pointing at right now before moving to parent.
								generate_controls_setup(outputfile, head);
								head = head->firstchild;
							}
							ControlNode *temp;
							while (head != (ControlNode*)&tree.treeroot){

								temp = head;
								if (head->sibling != nullptr){
									do {

										// Before moving to next sibling we should generate what we are pointing at right now.
										generate_controls_setup(outputfile, head);

										head = head->sibling;
										delete temp;
										
										while (head->firstchild != nullptr){
											// Before moving to next sibling we should generate what we are pointing at right now.
											generate_controls_setup(outputfile, head);
											head = head->firstchild;
										}

										temp = head;
									}while (head->sibling != nullptr);

									// We need to generate before leaving since current thing head is pointing is not generated.
									generate_controls_setup(outputfile, head);
								}

								head = head->parent;
								delete temp;
							}

							// We need to close the `setup method`.
							outputfile << "\t}\n};\n";

						}else {
							std::cerr << "Unknown tag found as child of ui tag." << '\n';
							goto jmp_ERROR_CLOSE;
						}
					}

					// Check that end tag we hit was "ui".
					if(xmlreader.name() != TagUi) {
						std::cerr << "Tag ui was not closed. Check tag endings inside of ui if ending tag exists.\n";
					}

				}else {
						std::cerr << "Unknown root tag found. Tag hit:" << name.toUtf8().toStdString() << '\n';
						goto jmp_ERROR_CLOSE;
				}
			}
		}

		if(xmlreader.hasError()) {
			std::cerr << "Error parsing XML: " << xmlreader.errorString().toStdString() << std::endl;
			goto jmp_ERROR_CLOSE;
		}
	}

	outputfile.flush();
	jmp_ERROR_CLOSE:
	// Free the QFile for the output before exiting if it was allocated.
	if(parser.isSet(outputoption)) {
			delete outputfile.device();
	}

	return 0;
}
