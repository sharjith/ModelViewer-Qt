#pragma once

class Point
{
public:
	Point() { _x = _y = _z = 0.0f; }
	Point(const float& x, const float& y, const float& z);
	~Point();

	void setParam(const float& x, const float& y, const float& z)
	{
		setX(x);
		setY(y);
		setZ(z);
	}

	void setX(const float& x) { _x = x; }
	void setY(const float& y) { _y = y; }
	void setZ(const float& z) { _z = z; }

	float getX() const { return _x; }
	float getY() const { return _y; }
	float getZ() const { return _z; }

	float distance(const Point& other);

private:
	float _x;
	float _y;
	float _z;
};
