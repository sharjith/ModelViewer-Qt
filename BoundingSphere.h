#pragma once

#include <QVector3D>

class BoundingSphere
{
public:
	BoundingSphere(const float& cx = 0.0f, const float& cy = 0.0f, const float& cz = 0.0f, const float& rad = 1.0f);
	virtual ~BoundingSphere(void);

	float getRadius() const { return _radius; }

	QVector3D getCenter() const { return _center; }

	void setRadius(const float& rad);
	void setCenter(const float& cx, const float& cy, const float& cz);
	void setCenter(const QVector3D& cen);

	void addSphere(const BoundingSphere& other);

private:
	QVector3D _center;
	float _radius;
};
