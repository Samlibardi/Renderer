#pragma once

template <class keyT, int uniqueID> class Handle {
	keyT m_key = ~(keyT());
public:
	explicit Handle(keyT key) : m_key(key) {}
	using key_type = keyT;
	
	explicit operator keyT() inline const { return m_key; }
	bool operator==(const Handle<keyT>& other) inline const { return this->m_key == other.m_key; }
	bool operator!=(const Handle<keyT>& other) inline const { return this->m_key != other.m_key; }
	bool operator>(const Handle<keyT>& other) inline const { return this->m_key > other.m_key;  }
	bool operator<(const Handle<keyT>& other) inline const { return  this->m_key < other.m_key; }
	Handle<keyT, uniqueID>& operator++() inline const { this->m_key++; return this; }

	bool isValid() inline const { return m_key != ~(keyT()); }

	friend class std::hash<Handle<keyT>>;
};

namespace std
{
	template<class T, int I> struct hash<Handle<T, I>> {
		inline size_t hash<Handle<T, I>>::operator()(const Handle<T, I>& val) {
			return std::hash<T>(val.m_key);
		}
	};
}