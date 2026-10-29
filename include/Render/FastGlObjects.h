#pragma once

// ---------------------------------------------------------------------------
// FastGlBuffer / FastGlVertexArray
//
// Drop-in replacements for the parts of QOpenGLBuffer and QOpenGLVertexArrayObject that the mesh classes use.
// Qt's wrappers register every object with the context (a guard per buffer, a signal connection per VAO) and
// searching those registries makes creating or destroying an object cost time proportional to the number of live
// objects. With the 14 buffers and VAOs a mesh owns, closing a 40,000-part model took about three minutes; the same
// GL objects deleted directly take milliseconds (measured with a standalone benchmark, 2026-10-29).
//
// Semantics kept from Qt:
//  - FastGlBuffer is a cheap handle: copies share one underlying GL buffer, and destroy() on any copy destroys it
//    for all of them (RenderableMesh keeps copies in _buffers).
//  - A buffer that is never explicitly destroyed is released when its last copy goes away.
// Differences:
//  - Nothing is registered with Qt. If an object is released while no suitable GL context is current, a buffer is
//    queued and deleted by the next FastGlBuffer::flushPendingDeletes() on a context of the same share group (the
//    viewport calls it every frame); a VAO is simply dropped, because VAOs belong to one context and are freed with it.
// ---------------------------------------------------------------------------

#include <QtGlobal>

#include <memory>

class QOpenGLContext;
class QOpenGLContextGroup;

class FastGlBuffer
{
public:
	enum Type { VertexBuffer = 0x8892, IndexBuffer = 0x8893 };
	enum UsagePattern
	{
		StreamDraw = 0x88E0, StreamRead = 0x88E1, StreamCopy = 0x88E2,
		StaticDraw = 0x88E4, StaticRead = 0x88E5, StaticCopy = 0x88E6,
		DynamicDraw = 0x88E8, DynamicRead = 0x88E9, DynamicCopy = 0x88EA
	};

	FastGlBuffer(Type type = VertexBuffer);

	Type type() const { return _type; }
	bool create();
	bool isCreated() const;
	void destroy();
	bool bind();
	void release();
	void setUsagePattern(UsagePattern usage);
	void allocate(const void* data, int count);
	void allocate(int count) { allocate(nullptr, count); }
	void write(int offset, const void* data, int count);
	bool read(int offset, void* data, int count);
	unsigned int bufferId() const;
	int size() const;

	// Deletes buffers whose owner went away without a usable context. Cheap when nothing is pending.
	static void flushPendingDeletes();

private:
	struct State
	{
		unsigned int id = 0;
		int size = 0;
		unsigned int usage = StaticDraw;
		QOpenGLContextGroup* group = nullptr;
		~State();
		void release();
	};

	Type _type;
	std::shared_ptr<State> _state;
};

class FastGlVertexArray
{
public:
	FastGlVertexArray() = default;
	~FastGlVertexArray() { destroy(); }
	FastGlVertexArray(const FastGlVertexArray&) = delete;
	FastGlVertexArray& operator=(const FastGlVertexArray&) = delete;

	bool create();
	bool isCreated() const { return _id != 0; }
	void destroy();
	void bind();
	void release();
	unsigned int objectId() const { return _id; }

private:
	unsigned int _id = 0;
	QOpenGLContext* _context = nullptr;
};
