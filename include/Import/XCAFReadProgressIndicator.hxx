#pragma once

#include <iostream>
#include <Message_ProgressIndicator.hxx>
#include <Message_ProgressRange.hxx>
#include <Message_ProgressScope.hxx>
#include <Standard_CString.hxx>
#include <Standard_Handle.hxx>
#include <Standard_Type.hxx>

class XCAFReadProgressIndicator;
DEFINE_STANDARD_HANDLE(XCAFReadProgressIndicator, Message_ProgressIndicator)

class XCAFReadProgressIndicator : public Message_ProgressIndicator
{
public:
	XCAFReadProgressIndicator();

	// OCCT RTTI
	DEFINE_STANDARD_RTTIEXT(XCAFReadProgressIndicator, Message_ProgressIndicator)

	// Overridden methods
	void Show(const Message_ProgressScope& theScope, const Standard_Boolean theForce) override;
	Standard_Boolean UserBreak() override;

	void Reset() override;

	// Optional: Override SetText to display scope names
	void SetText(const Standard_CString theText);

private:
	Standard_Real myLastShownProgress;
};
