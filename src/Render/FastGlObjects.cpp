#include "FastGlObjects.h"

#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>

#include <algorithm>
#include <cstring>
#include <mutex>
#include <vector>

namespace
{
struct PendingBuffer
{
	QOpenGLContextGroup* group;
	unsigned int id;
};

std::mutex& pendingMutex()
{
	static std::mutex m;
	return m;
}

std::vector<PendingBuffer>& pendingBuffers()
{
	static std::vector<PendingBuffer> v;
	return v;
}

QOpenGLExtraFunctions* currentFunctions()
{
	QOpenGLContext* ctx = QOpenGLContext::currentContext();
	return ctx ? ctx->extraFunctions() : nullptr;
}
} // namespace

// ---- FastGlBuffer ----------------------------------------------------------

FastGlBuffer::FastGlBuffer(Type type) : _type(type), _state(std::make_shared<State>()) {}

FastGlBuffer::State::~State()
{
	release();
}

void FastGlBuffer::State::release()
{
	lazy = false;
	if (id == 0)
		return;
	QOpenGLContext* ctx = QOpenGLContext::currentContext();
	if (ctx && ctx->shareGroup() == group)
	{
		ctx->extraFunctions()->glDeleteBuffers(1, &id);
	}
	else
	{
		std::lock_guard<std::mutex> lock(pendingMutex());
		pendingBuffers().push_back({ group, id });
	}
	id = 0;
	size = 0;
	group = nullptr;
}

bool FastGlBuffer::create()
{
	if (_state->id != 0)
		return true;
	QOpenGLContext* ctx = QOpenGLContext::currentContext();
	if (!ctx)
		return false;
	ctx->extraFunctions()->glGenBuffers(1, &_state->id);
	_state->group = ctx->shareGroup();
	_state->size = 0;
	return _state->id != 0;
}

void FastGlBuffer::createLazily()
{
	if (_state->id == 0)
		_state->lazy = true;
}

bool FastGlBuffer::isCreated() const
{
	return _state->id != 0 || _state->lazy;
}

void FastGlBuffer::destroy()
{
	_state->release();
}

bool FastGlBuffer::bind()
{
	if (_state->id == 0 && _state->lazy)
		create();
	QOpenGLExtraFunctions* f = currentFunctions();
	if (!f || _state->id == 0)
		return false;
	f->glBindBuffer(_type, _state->id);
	return true;
}

void FastGlBuffer::release()
{
	if (QOpenGLExtraFunctions* f = currentFunctions())
		f->glBindBuffer(_type, 0);
}

void FastGlBuffer::setUsagePattern(UsagePattern usage)
{
	_state->usage = static_cast<unsigned int>(usage);
}

void FastGlBuffer::allocate(const void* data, int count)
{
	QOpenGLExtraFunctions* f = currentFunctions();
	if (!f || _state->id == 0)
		return;
	f->glBufferData(_type, static_cast<GLsizeiptr>(count), data, _state->usage);
	_state->size = count;
}

void FastGlBuffer::write(int offset, const void* data, int count)
{
	if (QOpenGLExtraFunctions* f = currentFunctions())
		f->glBufferSubData(_type, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(count), data);
}

bool FastGlBuffer::read(int offset, void* data, int count)
{
	QOpenGLExtraFunctions* f = currentFunctions();
	if (!f)
		return false;
	const void* mapped = f->glMapBufferRange(_type, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(count), GL_MAP_READ_BIT);
	if (!mapped)
		return false;
	std::memcpy(data, mapped, static_cast<size_t>(count));
	f->glUnmapBuffer(_type);
	return true;
}

unsigned int FastGlBuffer::bufferId() const
{
	return _state->id;
}

int FastGlBuffer::size() const
{
	return _state->size;
}

void FastGlBuffer::flushPendingDeletes()
{
	QOpenGLContext* ctx = QOpenGLContext::currentContext();
	if (!ctx)
		return;
	std::vector<unsigned int> ids;
	{
		std::lock_guard<std::mutex> lock(pendingMutex());
		std::vector<PendingBuffer>& pending = pendingBuffers();
		if (pending.empty())
			return;
		QOpenGLContextGroup* group = ctx->shareGroup();
		auto split = std::stable_partition(pending.begin(), pending.end(),
			[group](const PendingBuffer& p) { return p.group != group; });
		for (auto it = split; it != pending.end(); ++it)
			ids.push_back(it->id);
		pending.erase(split, pending.end());
	}
	if (!ids.empty())
		ctx->extraFunctions()->glDeleteBuffers(static_cast<GLsizei>(ids.size()), ids.data());
}

// ---- FastGlVertexArray -----------------------------------------------------

bool FastGlVertexArray::create()
{
	if (_id != 0)
		return true;
	QOpenGLContext* ctx = QOpenGLContext::currentContext();
	if (!ctx)
		return false;
	ctx->extraFunctions()->glGenVertexArrays(1, &_id);
	_context = ctx;
	return _id != 0;
}

void FastGlVertexArray::destroy()
{
	if (_id == 0)
		return;
	// A VAO belongs to one context. Deleting it from another one could delete an unrelated VAO that happens to
	// have the same name there, so it is only deleted from its own context; otherwise it goes away with the context.
	QOpenGLContext* ctx = QOpenGLContext::currentContext();
	if (ctx && ctx == _context)
		ctx->extraFunctions()->glDeleteVertexArrays(1, &_id);
	_id = 0;
	_context = nullptr;
}

void FastGlVertexArray::bind()
{
	if (QOpenGLExtraFunctions* f = currentFunctions())
		f->glBindVertexArray(_id);
}

void FastGlVertexArray::release()
{
	if (QOpenGLExtraFunctions* f = currentFunctions())
		f->glBindVertexArray(0);
}
