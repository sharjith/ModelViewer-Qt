#pragma once

class BoundingSphere;
class IDrawable
{
public:
	virtual void render() = 0;
	virtual void select() = 0;
	virtual void deselect() = 0;
	virtual bool isSelected() const = 0;
	virtual BoundingSphere getBoundingSphere() const = 0;
};
