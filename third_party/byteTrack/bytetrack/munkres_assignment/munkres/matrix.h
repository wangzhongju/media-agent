/*
* Description: 矩阵类及相关操作
*/
#ifndef _MATRIX_H_
#define _MATRIX_H_

#include <initializer_list>
#include <cstdlib>
#include <ostream>
#include <cassert>
#include <cstdlib>
#include <algorithm>

namespace camera 
{

#define XYZMIN(x, y) (x)<(y)?(x):(y)
#define XYZMAX(x, y) (x)>(y)?(x):(y)


template <class T>
class Matrix 
{
public:
	// 构造函数
	Matrix()
	{
		m_rows = 0;
		m_columns = 0;
		m_matrix = nullptr;
	}

	Matrix(const size_t rows, const size_t columns)  
	{
		m_matrix = nullptr;
		resize(rows, columns);
	}

	Matrix(const std::initializer_list<std::initializer_list<T>> init) 
	{
		m_matrix = nullptr;
		m_rows = init.size();
		if ( m_rows == 0 ) 
		{
			m_columns = 0;
		} 
		else 
		{
			m_columns = init.begin()->size();
			if ( m_columns > 0 ) 
			{
				resize(m_rows, m_columns);
			}
		}

		size_t i = 0, j;
		for ( auto row = init.begin() ; row != init.end() ; ++row, ++i ) 
		{
			assert ( row->size() == m_columns && "All rows must have the same number of columns." );
			j = 0;
			for ( auto value = row->begin() ; value != row->end() ; ++value, ++j ) 
			{
				m_matrix[i][j] = *value;
			}
		}
	}

	// 拷贝构造函数
	Matrix(const Matrix<T> &other)  
	{
		if ( other.m_matrix != nullptr ) 
		{
			// copy arrays
			m_matrix = nullptr;
			resize(other.m_rows, other.m_columns);
			for ( size_t i = 0 ; i < m_rows ; i++ ) 
			{
				for ( size_t j = 0 ; j < m_columns ; j++ ) 
				{
					m_matrix[i][j] = other.m_matrix[i][j];
				}
			}
		} 
		else 
		{
			m_matrix = nullptr;
			m_rows = 0;
			m_columns = 0;
		}
	}

	// 操作符 = 重载
	Matrix<T> & operator= (const Matrix<T> &other)
	{
		if ( other.m_matrix != nullptr ) 
		{
			// copy arrays
			resize(other.m_rows, other.m_columns);
			for ( size_t i = 0 ; i < m_rows ; i++ ) 
			{
				for ( size_t j = 0 ; j < m_columns ; j++ ) 
				{
					m_matrix[i][j] = other.m_matrix[i][j];
				}
			}
		} 
		else 
		{
			// free arrays
			for ( size_t i = 0 ; i < m_columns ; i++ ) 
			{
				delete [] m_matrix[i];
			}

			delete [] m_matrix;

			m_matrix = nullptr;
			m_rows = 0;
			m_columns = 0;
		}

		return *this;
	}

	// 析构函数
	~Matrix()
	{
		if ( m_matrix != nullptr ) 
		{
			// free arrays
			for ( size_t i = 0 ; i < m_rows ; i++ ) 
			{
				delete [] m_matrix[i];
			}

			delete [] m_matrix;
		}
		m_matrix = nullptr;
	}
	
	// all operations modify the matrix in-place.
	void resize(const size_t rows, const size_t columns, const T default_value = 0) 
	{
		assert ( rows > 0 && columns > 0 && "Columns and rows must exist." );

		if ( m_matrix == nullptr ) 
		{
			// alloc arrays
			m_matrix = new T*[rows]; // rows
			for ( size_t i = 0 ; i < rows ; i++ ) 
			{
				m_matrix[i] = new T[columns]; // columns
			}

			m_rows = rows;
			m_columns = columns;
			clear();
		} 
		else 
		{
			// save array pointer
			T **new_matrix;
			// alloc new arrays
			new_matrix = new T*[rows]; // rows
			for ( size_t i = 0 ; i < rows ; i++ ) 
			{
				new_matrix[i] = new T[columns]; // columns
				for ( size_t j = 0 ; j < columns ; j++ ) 
				{
					new_matrix[i][j] = default_value;
				}
			}

			// copy data from saved pointer to new arrays
			size_t minrows = XYZMIN(rows, m_rows);
			size_t mincols = XYZMIN(columns, m_columns);
			for ( size_t x = 0 ; x < minrows ; x++ ) 
			{
				for ( size_t y = 0 ; y < mincols ; y++ ) 
				{
					new_matrix[x][y] = m_matrix[x][y];
				}
			}

			// delete old arrays
			if ( m_matrix != nullptr ) 
			{
				for ( size_t i = 0 ; i < m_rows ; i++ ) 
				{
					delete [] m_matrix[i];
				}

				delete [] m_matrix;
			}

			m_matrix = new_matrix;
		}

		m_rows = rows;
		m_columns = columns;
	}

    // 矩阵中所有元素赋值为0
	void clear() 
	{
		assert( m_matrix != nullptr );

		for ( size_t i = 0 ; i < m_rows ; i++ ) 
		{
			for ( size_t j = 0 ; j < m_columns ; j++ ) 
			{
				m_matrix[i][j] = 0;
			}
		}
	}

	// ()重载, 取x行y列的元素
	T& operator () (const size_t x, const size_t y) 
	{
		assert ( x < m_rows );
		assert ( y < m_columns );
		assert ( m_matrix != nullptr );
		return m_matrix[x][y];
	}

	const T& operator () (const size_t x, const size_t y) const 
	{
		assert ( x < m_rows );
		assert ( y < m_columns );
		assert ( m_matrix != nullptr );
		return m_matrix[x][y];
	}

    // 查找矩阵中元素的最小值
	const T mmin() const 
	{
		assert( m_matrix != nullptr );
		assert ( m_rows > 0 );
		assert ( m_columns > 0 );
		T min = m_matrix[0][0];

		for ( size_t i = 0 ; i < m_rows ; i++ ) 
		{
			for ( size_t j = 0 ; j < m_columns ; j++ ) 
			{
				min = std::min<T>(min, m_matrix[i][j]);
			}
		}

		return min;
	}

	// 查找矩阵中元素的最大值
	const T mmax() const 
	{
		assert( m_matrix != nullptr );
		assert ( m_rows > 0 );
		assert ( m_columns > 0 );
		T max = m_matrix[0][0];

		for ( size_t i = 0 ; i < m_rows ; i++ ) 
		{
			for ( size_t j = 0 ; j < m_columns ; j++ ) 
			{
				max = std::max<T>(max, m_matrix[i][j]);
			}
		}

		return max;
	}

	// 返回行列中最小值
	inline size_t minsize() { return ((m_rows < m_columns) ? m_rows : m_columns); }
	// 获取行数
	inline size_t columns() const { return m_columns;}
	// 获取列数
	inline size_t rows() const { return m_rows;}

	// << 操作符重载, 打印矩阵中的所有元素
	friend std::ostream& operator<<(std::ostream& os, const Matrix &matrix)
	{
		os << "Matrix:" << std::endl;
		for (size_t row = 0 ; row < matrix.rows() ; row++ )
		{
			for (size_t col = 0 ; col < matrix.columns() ; col++ )
			{
				os.width(8);
				os << matrix(row, col) << ",";
			}
			os << std::endl;
		}
		return os;
	}

private:
	T **m_matrix;
	size_t m_rows;
	size_t m_columns;
};

}
#endif /* !defined(_MATRIX_H_) */

