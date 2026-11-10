#pragma once

#include <Standard_GUID.hxx>

// The id of a STEP file's root product (PRODUCT(id, name, description): the id) travels from the STEP reader to the XCAF traversal as a
// TDataStd_AsciiString attribute with this GUID on the root's label, and becomes the node property "STEP_PRODUCT_ID" there.
inline const Standard_GUID& stepProductIdGuid()
{
    static const Standard_GUID guid("6f0f2c5e-3b1d-4a7e-9c84-2d5a8e1b7c33");
    return guid;
}
