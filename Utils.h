#ifndef UTILS_H_INCLUDED
#define UTILS_H_INCLUDED

#include <QImage>


inline QImage convertToGLFormat(const QImage& image)
{
    return image.convertToFormat(QImage::Format_RGBA8888).mirrored(); // flipped 32bit RGBA
}

#endif
