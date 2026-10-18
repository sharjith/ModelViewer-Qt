#pragma once

#include "IXCAFDocProcessor.hxx"
#include "XCAFDocProcessor.hxx"
#include "XCAFSTEPProcessor.hxx"
#include "XCAFIGESProcessor.hxx"
#include "XCAFBREPProcessor.hxx"
#include "XCAFVRMLProcessor.hxx"
#include <memory>
#include <string>
#include <algorithm>


class XCAFDocProcessorFactory
{
public:
    static std::unique_ptr<IXCAFDocProcessor> createProcessor(const std::string& extension);

private:
    static std::string toLowerCase(const std::string& str);
};

