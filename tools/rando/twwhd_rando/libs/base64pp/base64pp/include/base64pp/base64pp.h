//! @file base64pp.h
//! @brief This file defines the public functions
//! for the base64pp library
#ifndef BASE64PP_H
#define BASE64PP_H

#include "base64pp_export.h"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

//! base64pp API namespace
namespace base64pp
{

    //! @brief This function will encode a blob of data into a base64
    //! string.
    //! @param input - a span pointing to a binary blob to encode.
    //! @return a base64 string containing the encoded data.
    std::string BASE64PP_EXPORT encode(std::span<std::uint8_t const> input);

    //! @brief Overload of the encode function for string_view. This converts
    //! the string input to a span and calls the conventional `encode`.
    //! @param input - a string_view to be encoded into base64.
    //! @return a base64 string containing the encoded string.
    std::string BASE64PP_EXPORT encode_str(std::string_view input);

    //! @brief Decodes a base64 encoded string, returning an optional
    //! blob. If the decoding fails, it returns std::nullopt
    //! @param encoded_str - the base64 encoded string
    //! @return an optional containing a valid blob of data, if
    //! decoding was successful. Otherwise, returns std::nullopt
    //! @note this function accepts unpadded strings, if they are valid
    //! otherwise. It rejects odd-sized unpadded strings.
    std::optional<std::vector<std::uint8_t>> BASE64PP_EXPORT decode(std::string_view encoded_str);
} // namespace base64pp

#endif // BASE64PP_H
