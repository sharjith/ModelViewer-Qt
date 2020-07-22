#pragma once

class BoundingSphere;
class IDrawable
{
public:
	virtual void render() = 0;
	virtual BoundingSphere getBoundingSphere() const = 0;
};