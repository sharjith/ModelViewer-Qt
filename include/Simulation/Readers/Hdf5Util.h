#pragma once

// Small RAII / convenience layer over the HDF5 C API, shared by the result readers that sit on it (VTKHDF, MED). Header only;
// available only when the build has the HDF5 library (MV_HAVE_HDF5).

#if MV_HAVE_HDF5

#include <hdf5.h>

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <vector>

namespace hdf5util
{
	using CloseFn = herr_t (*)(hid_t);

	// Closes an HDF5 identifier when it goes out of scope.
	class Handle
	{
	public:
		Handle() = default;
		Handle(hid_t id, CloseFn close) : _id(id), _close(close) {}
		Handle(const Handle&) = delete;
		Handle& operator=(const Handle&) = delete;
		Handle(Handle&& other) noexcept : _id(other._id), _close(other._close) { other._id = -1; }
		Handle& operator=(Handle&& other) noexcept
		{
			if (this != &other)
			{
				reset();
				_id = other._id;
				_close = other._close;
				other._id = -1;
			}
			return *this;
		}
		~Handle() { reset(); }
		bool ok() const { return _id >= 0; }
		operator hid_t() const { return _id; }

	private:
		void reset()
		{
			if (_id >= 0 && _close)
				_close(_id);
			_id = -1;
		}
		hid_t _id = -1;
		CloseFn _close = nullptr;
	};

	// The library prints its error stack to stderr by default; probing for optional links would spam it.
	class ErrorSilencer
	{
	public:
		ErrorSilencer()
		{
			H5Eget_auto2(H5E_DEFAULT, &_func, &_data);
			H5Eset_auto2(H5E_DEFAULT, nullptr, nullptr);
		}
		~ErrorSilencer() { H5Eset_auto2(H5E_DEFAULT, _func, _data); }

	private:
		H5E_auto2_t _func = nullptr;
		void* _data = nullptr;
	};

	template <typename T> hid_t nativeType();
	template <> inline hid_t nativeType<float>() { return H5T_NATIVE_FLOAT; }
	template <> inline hid_t nativeType<double>() { return H5T_NATIVE_DOUBLE; }
	template <> inline hid_t nativeType<long long>() { return H5T_NATIVE_LLONG; }
	template <> inline hid_t nativeType<int>() { return H5T_NATIVE_INT; }
	template <> inline hid_t nativeType<unsigned char>() { return H5T_NATIVE_UCHAR; }

	inline bool linkExists(hid_t loc, const QString& path)
	{
		const QByteArray name = path.toUtf8();
		return H5Lexists(loc, name.constData(), H5P_DEFAULT) > 0;
	}

	inline Handle openDataset(hid_t loc, const QString& path)
	{
		const QByteArray name = path.toUtf8();
		return Handle(H5Dopen2(loc, name.constData(), H5P_DEFAULT), H5Dclose);
	}

	inline Handle openGroup(hid_t loc, const QString& path)
	{
		const QByteArray name = path.toUtf8();
		return Handle(H5Gopen2(loc, name.constData(), H5P_DEFAULT), H5Gclose);
	}

	inline std::vector<hsize_t> datasetDims(hid_t dataset)
	{
		std::vector<hsize_t> dims;
		Handle space(H5Dget_space(dataset), H5Sclose);
		if (!space.ok())
			return dims;
		const int rank = H5Sget_simple_extent_ndims(space);
		if (rank <= 0)
			return dims;
		dims.resize(static_cast<std::size_t>(rank));
		H5Sget_simple_extent_dims(space, dims.data(), nullptr);
		return dims;
	}

	inline bool datasetIsNumeric(hid_t dataset)
	{
		Handle type(H5Dget_type(dataset), H5Tclose);
		if (!type.ok())
			return false;
		const H5T_class_t cls = H5Tget_class(type);
		return cls == H5T_INTEGER || cls == H5T_FLOAT;
	}

	// Rows [first, first + rows) of a dataset (every column), converted to T. False when out of range or unreadable.
	template <typename T>
	bool readRows(hid_t dataset, hsize_t first, hsize_t rows, std::vector<T>& out)
	{
		out.clear();
		Handle space(H5Dget_space(dataset), H5Sclose);
		if (!space.ok())
			return false;
		const int rank = H5Sget_simple_extent_ndims(space);
		if (rank < 1)
			return false;
		std::vector<hsize_t> dims(static_cast<std::size_t>(rank));
		H5Sget_simple_extent_dims(space, dims.data(), nullptr);
		if (first + rows > dims[0])
			return false;
		hsize_t rowSize = 1;
		for (std::size_t d = 1; d < dims.size(); ++d)
			rowSize *= dims[d];
		if (rows == 0 || rowSize == 0)
			return true;
		std::vector<hsize_t> start(dims.size(), 0), count = dims;
		start[0] = first;
		count[0] = rows;
		if (H5Sselect_hyperslab(space, H5S_SELECT_SET, start.data(), nullptr, count.data(), nullptr) < 0)
			return false;
		Handle memory(H5Screate_simple(rank, count.data(), nullptr), H5Sclose);
		if (!memory.ok())
			return false;
		out.resize(static_cast<std::size_t>(rows * rowSize));
		if (H5Dread(dataset, nativeType<T>(), memory, space, H5P_DEFAULT, out.data()) < 0)
		{
			out.clear();
			return false;
		}
		return true;
	}

	// The whole dataset flattened (a 2-D one row by row). False when it does not exist or cannot be read.
	template <typename T>
	bool readAll(hid_t loc, const QString& path, std::vector<T>& out)
	{
		out.clear();
		if (!linkExists(loc, path))
			return false;
		Handle dataset = openDataset(loc, path);
		if (!dataset.ok() || !datasetIsNumeric(dataset))
			return false;
		const std::vector<hsize_t> dims = datasetDims(dataset);
		return !dims.empty() && readRows<T>(dataset, 0, dims[0], out);
	}

	inline bool readStringAttribute(hid_t object, const char* name, QString& out)
	{
		Handle attribute(H5Aopen(object, name, H5P_DEFAULT), H5Aclose);
		if (!attribute.ok())
			return false;
		Handle type(H5Aget_type(attribute), H5Tclose);
		if (!type.ok() || H5Tget_class(type) != H5T_STRING)
			return false;
		if (H5Tis_variable_str(type) > 0)
		{
			char* text = nullptr;
			if (H5Aread(attribute, type, &text) < 0 || !text)
				return false;
			out = QString::fromUtf8(text);
			H5free_memory(text);
			return true;
		}
		const std::size_t size = H5Tget_size(type);
		std::vector<char> buffer(size + 1, 0);
		if (H5Aread(attribute, type, buffer.data()) < 0)
			return false;
		out = QString::fromUtf8(buffer.data()).trimmed();
		return true;
	}

	template <typename T>
	bool readNumericAttribute(hid_t object, const char* name, std::vector<T>& out)
	{
		out.clear();
		Handle attribute(H5Aopen(object, name, H5P_DEFAULT), H5Aclose);
		if (!attribute.ok())
			return false;
		Handle space(H5Aget_space(attribute), H5Sclose);
		if (!space.ok())
			return false;
		const hssize_t count = H5Sget_simple_extent_npoints(space);
		if (count < 1)
			return false;
		out.resize(static_cast<std::size_t>(count));
		if (H5Aread(attribute, nativeType<T>(), out.data()) < 0)
		{
			out.clear();
			return false;
		}
		return true;
	}

	inline QStringList childNames(hid_t group)
	{
		QStringList names;
		H5G_info_t info;
		if (H5Gget_info(group, &info) < 0)
			return names;
		for (hsize_t i = 0; i < info.nlinks; ++i)
		{
			const ssize_t length = H5Lget_name_by_idx(group, ".", H5_INDEX_NAME, H5_ITER_INC, i, nullptr, 0, H5P_DEFAULT);
			if (length <= 0)
				continue;
			std::vector<char> name(static_cast<std::size_t>(length) + 1, 0);
			if (H5Lget_name_by_idx(group, ".", H5_INDEX_NAME, H5_ITER_INC, i, name.data(), name.size(), H5P_DEFAULT) >= 0)
				names << QString::fromUtf8(name.data());
		}
		return names;
	}
}

#endif // MV_HAVE_HDF5
