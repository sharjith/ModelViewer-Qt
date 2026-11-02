#include "Jt/JtImporter.h"
#include "JtSceneBuilder.h"
#include <JtData_Model.hxx>
#include <JtData_Limits.hxx>
#include <JtData_SkippedContent.hxx>
#include <QFileInfo>
#include <Standard_Failure.hxx>
namespace Jt {
ImportResult Importer::read(const QString& path,const Options& options) try {
    JtData_Limits::Scope scope([&]{options.check();},options.decodedAllocationBudget);
    options.check();const QFileInfo info(path);if(!info.exists() || info.size()<105) throw std::runtime_error("Cannot read JT header: file missing or truncated");
    JtData_SkippedContent::take(); // forget anything counted by an earlier import; the file's own table of contents is counted while it is read below
    Handle(JtData_Model) model=new JtData_Model(TCollection_ExtendedString(reinterpret_cast<const Standard_ExtCharacter*>(info.absoluteFilePath().utf16())));
    const auto root=model->Init();if(root.IsNull()) throw std::runtime_error("Cannot decode JT product structure");
    return Detail::buildScene(root,path,options);
} catch(const JtData_Limits::OutOfMemory& error) {
    throw std::runtime_error(error.what()); // a plain message, not the generic std::bad_alloc one
} catch(const Standard_Failure& error) {
    throw std::runtime_error(std::string("JT kernel error: ")+error.GetMessageString());
}
}
