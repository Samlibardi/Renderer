#pragma once

template<typename T> class Flags {
	using UnderlyingT = typename std::underlying_type<T>::type;
	UnderlyingT _val;
public:
	constexpr Flags() noexcept : _val(UnderlyingT()) {}
	constexpr Flags(const T& bit) noexcept : _val(static_cast<UnderlyingT>(bit)) {}
	constexpr Flags(const Flags<T>& other) noexcept = default;
	constexpr explicit Flags(const UnderlyingT& bit) noexcept : _val(static_cast<UnderlyingT>(bit)) {}

	constexpr Flags<T> operator| (const T& rhs) const noexcept { return static_cast<Flags<T>>(_val | static_cast<UnderlyingT>(rhs)); }
	constexpr Flags<T> operator& (const T& rhs) const noexcept { return static_cast<Flags<T>>(_val & static_cast<UnderlyingT>(rhs)); }
	constexpr Flags<T> operator^ (const T& rhs) const noexcept { return static_cast<Flags<T>>(_val ^ static_cast<UnderlyingT>(rhs)); }
	constexpr Flags<T> operator~ () const noexcept { return static_cast<Flags<T>>(~_val); }
	constexpr Flags<T>& operator|= (const T& rhs) noexcept { return *this = *this | rhs; }
	constexpr Flags<T>& operator&= (const T& rhs) noexcept { return *this = *this & rhs; }
	constexpr Flags<T>& operator^= (const T& rhs) noexcept { return *this = *this ^ rhs; }

	constexpr operator bool() { return _val != 0; }
};