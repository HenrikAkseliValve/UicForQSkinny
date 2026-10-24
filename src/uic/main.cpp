/*
 * Main for ui compiler for QSkinny.
*/
#include <QtCore/QCoreApplication>
#include <QtCore/QCommandLineParser>
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <QtCore/QString>
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
 * Control node is the n-tree node representing a control. Tree is generated parent to child. Parent only connections
 * to one sibling which connections to other childer. This done because easy dynamic response and we most likelly
 * have to multiple passes of generation.
*/
typedef struct ControlNode{
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
	struct {
		bool prefixed:1;
	} flags;
}ControlNode;

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

/*
 * Just string for the "->" which is used in C/C++ reference member of pointer.
*/
static const QString RefPointer = QStringLiteral("->");

/*
 * Parse a internals of pure text tag. Needs tag name for the error messages.
*/
QString static parse_text_tag(QXmlStreamReader *reader, QString nodename) {
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
 * Generate the setup commands for given control.
*/
static void generate_controls_setup(QTextStream &stream, ControlNode *node) {
	// Generate the allocation command.
	// NOTE: We do not need to deacllocate long as QQuick scene had pointer to this.
	stream << "\t\t" << node->variable_name << " = new " << node->control_name << "(" << node->parent->variable_name << ");\n";
	// Set the variable name as the object name for easier to find the control.
	stream << "\t\t" << node->variable_name << RefPointer << "setObjectName(QString(\""<< node->variable_name << "\"));\n\n";
}

/*
 * Start of the program. Drives the compilation.
*/
int main(int argn, char **argv){

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
					while((token = xmlreader.readNext()) != QXmlStreamReader::EndElement && !xmlreader.hasError()) {

						// Every newline and whitespace is reported as a character node.
						// We could skip to next element but that would assume user has made perfect syntax and no character node
						// was but in error. Hence, we need to handle the characters nodes and check `isWhitespace` to report
						// error and allow whitespace.
						if((token == QXmlStreamReader::Characters && xmlreader.isWhitespace()) || token == QXmlStreamReader::Comment) continue;

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

							while((token = xmlreader.readNext()) != QXmlStreamReader::EndElement || xmlreader.name() != TagStruct) {
								switch(token) {
									case QXmlStreamReader::TokenType::Invalid:
										std::cerr << "Invalid token encountered.\n";
										goto jmp_ERROR_CLOSE;
									case QXmlStreamReader::TokenType::StartElement:
										{
											QStringView name = xmlreader.name();
											if(false) {
												//TODO: Other stuff.
											}
											else {
												bool prefixed = false;
												if (name.startsWith(QString("c."))){
													if(name.length() == 2){
														std::cerr << "Syntax error. No control class was given after \"c.\".";
														goto jmp_ERROR_CLOSE;
													}
													name = name.slice(2);
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
											if (!(processinghead->control_name == name || (processinghead->flags.prefixed && processinghead->control_name == name.slice(2)))){
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
