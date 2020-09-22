#ifndef GLMATERIAL_H
#define GLMATERIAL_H

#include <QVector3D>

class GLMaterial
{
public:
    enum class PredefinedMaterials {BRASS, BRONZE, COPPER, GOLD, SILVER, CHROME,
                                RUBY, EMERALD, TURQUOISE, PEARL, JADE, OBSIDIAN,
                                RED_PLASTIC, GREEN_PLASTIC, CYAN_PLASTIC, YELLOW_PLASTIC, WHITE_PLASTIC, BLACK_PLASTIC,
                                RED_RUBBER, GREEN_RUBBER, CYAN_RUBBER, YELLOW_RUBBER, WHITE_RUBBER, BLACK_RUBBER};
public:
    GLMaterial();
    GLMaterial(QVector3D ambient, QVector3D diffuse, QVector3D specular, QVector3D emissive, float shininess, bool metallic = true, float opacity = 1.0f);
    GLMaterial(QVector3D albedo, float metalness, float roughness, float opacity);

    QVector3D ambient() const;
    void setAmbient(const QVector3D &ambient);

    QVector3D diffuse() const;
    void setDiffuse(const QVector3D &diffuse);

    QVector3D specular() const;
    void setSpecular(const QVector3D &specular);

    QVector3D emissive() const;
    void setEmissive(const QVector3D &emissive);

    float shininess() const;
    void setShininess(float shininess);

    bool metallic() const;
    void setMetallic(bool metallic);

    QVector3D albedoColor() const;
    void setAlbedoColor(const QVector3D &albedoColor);

    float metalness() const;
    void setMetalness(float metalness);

    float roughness() const;
    void setRoughness(float roughness);

    float opacity() const;
    void setOpacity(float opacity);

    static GLMaterial getPredefinedMaterial(GLMaterial::PredefinedMaterials type);
    static GLMaterial BRASS();
    static GLMaterial BRONZE();
    static GLMaterial COPPER();
    static GLMaterial GOLD();
    static GLMaterial SILVER();
    static GLMaterial CHROME();
    static GLMaterial RUBY();
    static GLMaterial EMERALD();
    static GLMaterial TURQUOISE();
    static GLMaterial PEARL();
    static GLMaterial JADE();
    static GLMaterial OBSIDIAN();
    static GLMaterial RED_PLASTIC();
    static GLMaterial GREEN_PLASTIC();
    static GLMaterial CYAN_PLASTIC();
    static GLMaterial YELLOW_PLASTIC();
    static GLMaterial WHITE_PLASTIC();
    static GLMaterial BLACK_PLASTIC();
    static GLMaterial RED_RUBBER();
    static GLMaterial GREEN_RUBBER();
    static GLMaterial CYAN_RUBBER();
    static GLMaterial YELLOW_RUBBER();
    static GLMaterial WHITE_RUBBER();
    static GLMaterial BLACK_RUBBER();
    static GLMaterial DEFAULT_MAT();

private:
    void setAlbedoFromADS();
private:
    QVector3D _ambient;
    QVector3D _diffuse;
    QVector3D _specular;
    QVector3D _emissive;
    float _shininess;
    bool _metallic;

    QVector3D _albedoColor;
    float     _metalness;
    float     _roughness;

    float _opacity;
};

#endif // GLMATERIAL_H
