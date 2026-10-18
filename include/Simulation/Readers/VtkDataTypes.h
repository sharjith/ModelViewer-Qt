#pragma once

// Scalar-type and raw-buffer helpers shared by the VTK XML (.vtu) and legacy (.vtk) readers.
// QtCore + standard library only.

#include <QString>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace vtkio
{
	enum class VtkType { Invalid, Int8, UInt8, Int16, UInt16, Int32, UInt32, Int64, UInt64, Float32, Float64 };

	inline std::size_t vtkTypeSize(VtkType t)
	{
		switch (t)
		{
		case VtkType::Int8: case VtkType::UInt8:                          return 1;
		case VtkType::Int16: case VtkType::UInt16:                        return 2;
		case VtkType::Int32: case VtkType::UInt32: case VtkType::Float32: return 4;
		case VtkType::Int64: case VtkType::UInt64: case VtkType::Float64: return 8;
		case VtkType::Invalid: break;
		}
		return 0;
	}

	inline bool vtkTypeIsFloat(VtkType t) { return t == VtkType::Float32 || t == VtkType::Float64; }
	inline bool vtkTypeIsUnsigned(VtkType t)
	{
		return t == VtkType::UInt8 || t == VtkType::UInt16 || t == VtkType::UInt32 || t == VtkType::UInt64;
	}

	template <class T>
	void reverseBytes(T& v)
	{
		unsigned char* p = reinterpret_cast<unsigned char*>(&v);
		std::reverse(p, p + sizeof(T));
	}

	template <class Src, class Dst>
	void convertBlock(const char* p, std::size_t count, bool swap, Dst* out)
	{
		for (std::size_t i = 0; i < count; ++i)
		{
			Src v;
			std::memcpy(&v, p + i * sizeof(Src), sizeof(Src));
			if (swap)
				reverseBytes(v);
			out[i] = static_cast<Dst>(v);
		}
	}

	// `bytes` raw bytes of element type `type` (byte order differing from the host's when `swap`) ->
	// vector<Dst>, converting element by element.
	template <class Dst>
	bool convertBinaryBytes(const char* p, std::size_t bytes, VtkType type, bool swap, std::vector<Dst>& out, QString& err)
	{
		const std::size_t size = vtkTypeSize(type);
		if (size == 0)
		{
			err = QStringLiteral("unknown data type");
			return false;
		}
		if (bytes % size != 0)
		{
			err = QStringLiteral("binary data size is not a multiple of the element size");
			return false;
		}
		const std::size_t count = bytes / size;
		out.resize(count);
		switch (type)
		{
		case VtkType::Int8:    convertBlock<std::int8_t>(p, count, swap, out.data());   break;
		case VtkType::UInt8:   convertBlock<std::uint8_t>(p, count, swap, out.data());  break;
		case VtkType::Int16:   convertBlock<std::int16_t>(p, count, swap, out.data());  break;
		case VtkType::UInt16:  convertBlock<std::uint16_t>(p, count, swap, out.data()); break;
		case VtkType::Int32:   convertBlock<std::int32_t>(p, count, swap, out.data());  break;
		case VtkType::UInt32:  convertBlock<std::uint32_t>(p, count, swap, out.data()); break;
		case VtkType::Int64:   convertBlock<std::int64_t>(p, count, swap, out.data());  break;
		case VtkType::UInt64:  convertBlock<std::uint64_t>(p, count, swap, out.data()); break;
		case VtkType::Float32: convertBlock<float>(p, count, swap, out.data());         break;
		case VtkType::Float64: convertBlock<double>(p, count, swap, out.data());        break;
		case VtkType::Invalid: break;
		}
		return true;
	}
}
