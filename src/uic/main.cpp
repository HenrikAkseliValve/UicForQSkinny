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
							outputfile << QByteArray("struct ") << classname << " {\n";
							while(xmlreader.readNext() != QXmlStreamReader::EndElement){
								;
							}

							// Check that end tag we hit was "struct".
							if(xmlreader.name() != TagStruct) {
								std::cerr << "Tag struct was not closed. Check tag endings inside of struct if ending tag exists.\n";
							}

							outputfile << QByteArray("}\n");
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