/*
 * Main for ui compiler for QSkinny.
*/
#include <QtCore/QCoreApplication>
#include <QtCore/QCommandLineParser>
#include <QtCore/QFile>
#include <QtXml/QDomDocument>
#include <qskinny/QskGlobal.h>
#include <iostream>

#ifndef PROGNAME
#define PROGNAME qskuic
#endif
#define PROGNAME_STR QT_STRINGIFY(PROGNAME)

#ifndef VERSION
#define VERSION git
#endif
#define VERSION_STR QT_STRINGIFY(VERSION)

/*
 * Start of the program. Drives the compilation.
*/
int main(int argn, char **argv){

	QCoreApplication app(argn, argv);
	QCoreApplication::setApplicationName(PROGNAME_STR);
	QCoreApplication::setApplicationVersion(VERSION_STR);

	QCommandLineParser parser;
	parser.setApplicationDescription("User interface compiler for QSkinny version " QSK_VERSION_STR);
	parser.addHelpOption();
	parser.addVersionOption();
	parser.addPositionalArgument("qskuicfile", "Source UIC file.", "<qskuic file>");

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
	
	{
		// PARSING STATE MACHINE
		//           ui
		// UIC_ROOT ──🠞 UIC ──🠞 
		//              │ 🠝
		//  exportmacro └─┘
		//              
		//
		QDomNode node = srcxml.documentElement();
		QDomElement ele;

		QString exportmacro = QStringLiteral("");

		while(!node.isNull()) {

			// Get Node type to decide next operation.
			QDomNode::NodeType type = node.nodeType();

			if(type == QDomNode::NodeType::ElementNode) {
				ele = node.toElement();
				if(ele.tagName() == "ui") {
					node = ele.firstChild();

					if(node.isElement()) {
						ele = node.toElement();
						if(ele.tagName() == "exportmacro") {
							exportmacro = ele.text();
							std::cout << "Export tag found: " << exportmacro.toStdString() << '\n';
						}
					}

				}
				else{
					std::cout << "Unknown tag found." << '\n';
				}
			}
			else{
				std::cout << "Unknown node found." << '\n';
			}

			node = node.nextSibling();
		}
	}
	return 0;
}
