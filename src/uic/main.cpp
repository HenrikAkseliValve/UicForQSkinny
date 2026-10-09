/*
 * Main for ui compiler for QSkinny.
*/
#include <QtCore/QCoreApplication>
#include <QtCore/QCommandLineParser>
#include <QtXml/QDomDocument>
#include <QtCore/QFile>
#include <QtCore/QTextStream>
#include <qskinny/QskGlobal.h>
#include <iostream>
#include <filesystem>

/*
 * If name of the program is not given from the compiler then default to "qskuic".
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
 * Sepcial variable to indicate empty string. Used as intialization for some QStrings.
*/
static const QString Empty = QStringLiteral("");

/*
 * List of tag name in constant strings to avoid writting errors when used multiple times.
*/
static const QString ExportMacro = QStringLiteral("exportmacro");
static const QString Author = QStringLiteral("author");
static const QString Description = QStringLiteral("description");
static const QString Include = QStringLiteral("include");

/*
 * Parse a internals of pure text tag. Needs tag name for the error message.
*/
QString static parse_text_tag(QDomElement ele, QString nodename){
	auto name = ele.firstChild();
	if(name.isText()) {
		return name.toText().data();
	}
	else {
		std::cerr << "Inside " << nodename.toStdString() << " tag must be text." << '\n';
		exit(1);
	}
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
	if(srcfiles.count() == 0){
		std::cout << "No source files to compile. Exiting..." << std::endl;
		return 0;
	}

	// Allow multiple files to specified but do not advertise.
	// Feature maybe removed.
	const QString srcfile = srcfiles.at(0);
	QDomDocument srcxml(srcfile);
	{
		QFile file(srcfile);
		if (!file.open(QIODevice::ReadOnly)) {
			std::cerr << "Source file not found!" << std::endl;
			return 1;
		}
		if (!srcxml.setContent(&file)) {
			std::cerr << "Source content reading failed!" << std::endl;
			file.close();
			return 1;
		}
		file.close();
	}

	QTextStream outputfile;
	if(parser.isSet(outputoption)) {
		// Create the output file and path.
		std::filesystem::path outputfilepath = parser.value(outputoption).toStdString();
		if (std::filesystem::exists(outputfilepath))
		{
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
		QDomNode node = srcxml.documentElement();
		QDomElement ele;
		QDomText text;

		// Macro added class to control export in some languages.
		QString exportmacro = QStringLiteral("");

		// List to store includes.
		QList<QString> systemincludes;
		QList<QString> localincludes;

		// String to store information used in overviwer comment.
		QString description = Empty;
		QString author = Empty;

		while(!node.isNull()) {

			// Get Node type to decide next operation.
			QDomNode::NodeType type = node.nodeType();

			if(type == QDomNode::NodeType::ElementNode) {
				ele = node.toElement();
				if(ele.tagName() == "ui") {

					// Check optional version attribute before moving to parse metadata.
					QString version = ele.attribute("version", "0.0");
					
					// Grab first child of "ui" root. Everything under here is considered 
					QDomNode uinode = ele.firstChild();
					
					do{

						if(uinode.isElement()) {
							ele = uinode.toElement();

							if(ele.tagName() == ExportMacro) {
								exportmacro = parse_text_tag(ele, ExportMacro);
							}
							else if(ele.tagName() == Author) {
								author = parse_text_tag(ele, Author);
							}
							else if(ele.tagName() == Description) {
								description = parse_text_tag(ele, Description);
							}
							else if(ele.tagName() == Include) {

								// Is this include local or system. May effect output.
								enum {LOCAL, SYSTEM} localorsystem = SYSTEM;

								// Attributes are not part of the node tree. They are nodes but
								// under per element under mapping structure.
								auto attributes = ele.attributes();
								for(int i = 0; i < attributes.length(); i++) {
									auto attribute = attributes.item(i).toAttr();

									if(attribute.name() == "location") {
										QString value = attribute.value();
										if(value == "local"){
											localorsystem = LOCAL;
										}
										else if(value == "system") {
											localorsystem = SYSTEM;
										}
										else {
											std::cerr << "Unknown value for attribute location in " << Include.toStdString() << " tag." << '\n';
											goto jmp_ERROR_CLOSE;
										}
									}
									else {
										std::cerr << "Unknown attribute in " << Include.toStdString() << " tag." << '\n';
										goto jmp_ERROR_CLOSE;
									}
								}

								// Read the incldue values text and generate a include.
								auto qtext = parse_text_tag(ele, Include);
								if (localorsystem == LOCAL){
									localincludes.append(qtext);
								}
								else{
									systemincludes.append(qtext);
								}
							}
							else if(ele.tagName() == "struct") {

								QByteArray buffer;

								// Generate the overviwer comment message to the output buffer.
								outputfile << QByteArray("/*\nThis file was generated by " PROGNAME_STR ".\n");

								if(description != Empty) outputfile << description.toUtf8() << QByteArray("\n");
								if(author != Empty) outputfile << QByteArray("Author: ") + author.toUtf8() + QByteArray("\n");
								
								outputfile << QByteArray("*/\n");

								// Go throught the includes
								// TODO: Options to effect default sort which is order or appearance. Examples, alphapetical sorting or mix of system and local includes.
								for(auto include : systemincludes) {
									outputfile << QByteArray("#include <") << include.toUtf8() << ">\n";
								}
								for(auto include : localincludes) {
									outputfile << QByteArray("#include \"") << include.toUtf8() << "\"\n";
								}

								// Class to be generated.
								QString classname = Empty;

								// Handle attributes for the "class" tag.
								auto attributes = ele.attributes();
								for(int i = 0; i < attributes.length(); i++) {
									auto attribute = attributes.item(i).toAttr();

									if(attribute.name() == "name") {
										QString value = attribute.value();
										classname = value;
									}
								}

								// If name attribute was not provided thrown an error.
								if (classname == Empty){
									std::cerr << "Syntax error a `class` tag did not have an name attribute. This is mandatory." << '\n';
									goto jmp_ERROR_CLOSE;
								}

								// Start generating the struct.
								outputfile << QByteArray("struct ") << classname << " {\n";

								;

								// Close the struct bracket.
								outputfile << QByteArray("}\n");
							}
						}
						uinode = uinode.nextSibling();
					}while(!uinode.isNull());

				}
				else{
					std::cerr << "Unknown tag found." << '\n';
					goto jmp_ERROR_CLOSE;
				}
			}
			else{
				std::cerr << "Unknown node found." << '\n';
				goto jmp_ERROR_CLOSE;
			}

			node = node.nextSibling();
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
