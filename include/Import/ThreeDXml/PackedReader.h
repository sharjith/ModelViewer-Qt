#pragma once
#include "Document.h"
#include <QtEndian>
#include <cmath>
#include <cstring>

namespace ThreeDXml {
// Raw byte/count/float reads deliberately preserve the packed-bit reservoir.
class PackedReader {
public:
    explicit PackedReader(const QByteArray& bytes) : data(bytes) {}
    const QByteArray& data;
    qsizetype pos = 0;
    quint64 reservoir = 0;
    unsigned available = 0;
    const char* raw(qsizetype n) {
        check(n >= 0 && pos <= data.size()-n, "truncated raw data");
        const char* value=data.constData()+pos; pos+=n; return value;
    }
    quint8 byte() { return static_cast<quint8>(*raw(1)); }
    quint32 count(quint32 maximum=10000000) {
        quint32 v=byte(); if(v==255) v=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(raw(4)));
        if(v>maximum) throw Error(QStringLiteral("UVR count %1 exceeds %2 at byte %3").arg(v).arg(maximum).arg(pos));
        return v;
    }
    float scalar() {
        quint32 bits=qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(raw(4)));
        float v; std::memcpy(&v,&bits,4); check(std::isfinite(v),"nonfinite float"); return v;
    }
    aiVector3D vector() { const float x=scalar(),y=scalar(),z=scalar(); return {x,y,z}; }
    void expect(const char* hex) {
        const QByteArray wanted=QByteArray::fromHex(hex);
        check(QByteArray(raw(wanted.size()),wanted.size())==wanted,"unsupported record prefix");
    }
    quint32 bits(unsigned width) {
        check(width>0 && width<=32,"invalid packed width");
        while(available<width) {
            reservoir|=quint64(qFromLittleEndian<quint32>(reinterpret_cast<const uchar*>(raw(4))))<<available;
            available+=32;
        }
        const quint32 v=static_cast<quint32>(reservoir&((quint64(1)<<width)-1));
        reservoir>>=width; available-=width; return v;
    }
private:
    static void check(bool ok,const char* reason) { if(!ok) throw Error(QStringLiteral("Unsupported/corrupt UVR: ")+QString::fromLatin1(reason)); }
};
}
