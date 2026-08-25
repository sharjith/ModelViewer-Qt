#include "XCAFDocProcessorFactory.hxx"

std::unique_ptr<IXCAFDocProcessor> XCAFDocProcessorFactory::createProcessor(const std::string& extension)
{
    std::string lowerExt = toLowerCase(extension);

    if (lowerExt == "step" || lowerExt == "stp")
    {
        return std::make_unique<XCAFSTEPProcessor>();
    }
    else if (lowerExt == "iges" || lowerExt == "igs")
    {
        return std::make_unique<XCAFIGESProcessor>();
    }
    else if (lowerExt == "brep" || lowerExt == "rle")
    {
        return std::make_unique<XCAFBREPProcessor>();
    }
    else if (lowerExt == "wrl" || lowerExt == "vrml")
    {
        return std::make_unique<XCAFVRMLProcessor>();
    }
    else
    {
        return nullptr;
    }
}

     std::string XCAFDocProcessorFactory::toLowerCase(const std::string& str)
    {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(), ::tolower);
        return result;
    }