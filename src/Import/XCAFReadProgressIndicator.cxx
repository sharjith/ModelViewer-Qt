#include "XCAFReadProgressIndicator.hxx"
#include "MainWindow.h"

IMPLEMENT_STANDARD_RTTIEXT(XCAFReadProgressIndicator, Message_ProgressIndicator)

XCAFReadProgressIndicator::XCAFReadProgressIndicator()
    :myLastShownProgress(-1)
{
    // Initialization if needed
}

void XCAFReadProgressIndicator::Show(const Message_ProgressScope& theScope, const Standard_Boolean theForce)
{
    Standard_Real pos = GetPosition() * 100.0;
    if (static_cast<int>(pos) != myLastShownProgress || theForce)
    {
        std::cout << "\rProgress: " << static_cast<int>(pos) << "%" << std::flush;
        myLastShownProgress = static_cast<int>(pos);
		MainWindow::setProgressValue(static_cast<int>(pos));
    }
}

Standard_Boolean XCAFReadProgressIndicator::UserBreak()
{
    return MainWindow::isFileLoadCancelRequested() ? Standard_True : Standard_False;
}

void XCAFReadProgressIndicator::Reset()
{
    myLastShownProgress = -1;
    std::cout << std::endl;
}


void XCAFReadProgressIndicator::SetText(const Standard_CString theText)
{
    if (theText)
    {
        std::cout << "Current operation: " << theText << std::endl;
    }
}
