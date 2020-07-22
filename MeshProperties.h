#ifndef MESHPROPERTIES_H
#define MESHPROPERTIES_H

#include <QObject>

class TriangleMesh;
class MeshProperties : public QObject
{
    Q_OBJECT
public:
    explicit MeshProperties(TriangleMesh *mesh, QObject *parent = nullptr);

    TriangleMesh *mesh() const;
    void setMesh(TriangleMesh *mesh);

    std::vector<float> meshPoints() const;

    float surfaceArea() const;

    float volume() const;

signals:

private:
    void calculateSurfaceAreaAndVolume();

private:
    TriangleMesh *_mesh;
    std::vector<float> _meshPoints;
    float _surfaceArea;
    float _volume;
};

#endif // MESHPROPERTIES_H
