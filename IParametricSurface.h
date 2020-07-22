#pragma once

#include <glm/gtc/constants.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>

class Point;
class IParametricSurface
{
public:

	virtual float firstUParameter() const = 0;
	virtual float firstVParameter() const = 0;
	virtual float lastUParameter() const = 0;
	virtual float lastVParameter() const = 0;
	virtual Point pointAtParameter(const float& u, const float& v) = 0;
	virtual glm::vec3 normalAtParameter(const float& u, const float& v) = 0;
};
