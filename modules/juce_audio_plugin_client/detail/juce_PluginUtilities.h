/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2022 - Raw Material Software Limited

   JUCE is an open source library subject to commercial or open-source
   licensing.

   By using JUCE, you agree to the terms of both the JUCE 7 End-User License
   Agreement and JUCE Privacy Policy.

   End User License Agreement: www.juce.com/juce-7-licence
   Privacy Policy: www.juce.com/juce-privacy-policy

   Or: You may also use this code under the terms of the GPL v3 (see
   www.gnu.org/licenses).

   JUCE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

#pragma once

#include <juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h>

namespace juce::detail
{

namespace custom
{

template<typename T, size_t SIZE = 0>
class LockFreeFIFO
{
  public:
    /// @brief Constructs a fifo when the SIZE parameter is > 0. Size must be an
    /// integral power of two.
    LockFreeFIFO()
        requires(SIZE > 0);
    /// @brief Constructs a fifo when the SIZE parameter is 0, this allows
    /// runtime decisions about how big the fifo needs to be.
    explicit LockFreeFIFO(size_t minSize)
        requires(SIZE == 0);
    ~LockFreeFIFO() = default;

    /// @brief Pops an element from the fifo.
    /// @param element The return value. Will be a default constructed T() if
    /// fifo is empty.
    /// @return True if the element was popped successfully or false if the fifo
    /// is empty.
    /// @note Thread: \b Consumer
    bool popElement(T& element) noexcept;
    bool popElement() noexcept;

    const T& peek() const noexcept;

    /// @brief Pushes an element into the fifo.
    /// @param value The value to push into the fifo.
    /// @return True if the push was successful or false if the fifo is full.
    /// @note Thread: \b Producer
    bool pushElement(T&& value) noexcept;
    bool pushElement(const T& value) noexcept;

    /// @brief Pops a number of elements off at once into an array of data.
    ///
    /// Element type must be trivially copyable.
    /// @param dst The location to store the elements.
    /// @param numValues The number of values you want to pop off.
    /// @return Returns the number of elements successfully popped into dst. If
    /// this returns 0 the fifo is empty.
    /// @note Thread: \b Consumer
    size_t popElements(T* dst, size_t numValues) noexcept
        requires std::is_trivially_copyable_v<T>;

    /// @brief Pushes an array of elements into the fifo.
    ///
    /// Element type must be trivially copyable.
    /// @param values The location of the elements to push.
    /// @param numValues The number of elements to push.
    /// @return Returns the number of elements successfully pushed, if this
    /// returns 0 the fifo is full.
    /// @note Thread: \b Producer
    size_t pushElements(const T* values, size_t numValues) noexcept
        requires std::is_trivially_copyable_v<T>;

    /// @brief Returns the current number of elements in the fifo.
    /// @return The number of elements in the fifo.
    /// @note Thread: \b Any
    size_t getSize() const noexcept;

    /// @brief Returns the remaining space available in the fifo.
    /// @return The remaining space in the fifo.
    /// @note Thread: \b Any
    size_t getSpaceAvailable() const noexcept;

    /// @brief Returns the maximum capacity of the fifo. This is going to be one
    /// less than the underlying data structures size due to the read/write
    /// pointer implementation.
    /// @return The maximum number of values that can be in the fifo.
    /// @note Thread: \b Any
    size_t getCapacity() const noexcept;

  private:
    alignas(64) std::atomic<size_t> m_writeIndex{ 0 };
    alignas(64) size_t m_cachedWrite = 0;

    alignas(64) std::atomic<size_t> m_readIndex{ 0 };
    alignas(64) size_t m_cachedRead = 0;

    alignas(64) std::vector<T> m_data;
    std::array<T, SIZE> m_stackData;

    size_t m_bitMask;
};

template<typename T, size_t SIZE>
LockFreeFIFO<T, SIZE>::LockFreeFIFO()
    requires(SIZE > 0)
  : m_bitMask(SIZE - 1)
{
    static_assert(std::atomic<size_t>::is_always_lock_free);
}

template<typename T, size_t SIZE>
LockFreeFIFO<T, SIZE>::LockFreeFIFO(size_t minSize)
    requires(SIZE == 0)
{
    // jassert(minSize > 1, "Size must be 2 or greater.");
    static_assert(std::atomic<size_t>::is_always_lock_free);
    const auto nearestLog2 = 1 << (size_t)std::ceil(std::log2((double)minSize));
    m_data.resize(nearestLog2, T());
    m_bitMask = nearestLog2 - 1;
}

template<typename T, size_t SIZE>
bool
LockFreeFIFO<T, SIZE>::popElement(T& element) noexcept
{
    const auto writePtr = m_writeIndex.load(std::memory_order::acquire);
    auto readPtr        = m_cachedRead;

    // Check if empty
    if ((readPtr & m_bitMask) == (writePtr & m_bitMask))
    {
        element = T();
        return false;
    }

    if constexpr (SIZE == 0)
    {
        element = std::move(m_data[readPtr & m_bitMask]);
    }
    else
    {
        element = std::move(m_stackData[readPtr & m_bitMask]);
    }

    auto nextRead = readPtr + 1;
    m_readIndex.store(nextRead, std::memory_order::release);
    m_cachedRead = nextRead;
    return true;
}

template<typename T, size_t SIZE>
bool
LockFreeFIFO<T, SIZE>::popElement() noexcept
{
    const auto writePtr = m_writeIndex.load(std::memory_order::acquire);
    auto readPtr        = m_cachedRead;

    // Check if empty
    if ((readPtr & m_bitMask) == (writePtr & m_bitMask))
    {
        return false;
    }

    auto nextRead = readPtr + 1;
    m_readIndex.store(nextRead, std::memory_order::release);
    m_cachedRead = nextRead;

    return true;
}

template<typename T, size_t SIZE>
const T&
LockFreeFIFO<T, SIZE>::peek() const noexcept
{
    const auto writePtr = m_writeIndex.load(std::memory_order::acquire);
    auto readPtr        = m_cachedRead;

    // Check if empty
    if ((readPtr & m_bitMask) == (writePtr & m_bitMask))
    {
        // jassertfalse;
        return T();
    }

    if constexpr (SIZE == 0)
    {
        return m_data[readPtr & m_bitMask];
    }
    else
    {
        return m_stackData[readPtr & m_bitMask];
    }
}

template<typename T, size_t SIZE>
bool
LockFreeFIFO<T, SIZE>::pushElement(T&& value) noexcept
{
    auto currentRead        = m_readIndex.load(std::memory_order::acquire);
    const auto currentWrite = m_cachedWrite;
    const auto nextWrite    = currentWrite + 1;

    if ((nextWrite & m_bitMask) == (currentRead & m_bitMask))
    {
        return false;
    }

    // Store the new element
    if constexpr (SIZE == 0)
    {
        m_data[currentWrite & m_bitMask] = std::move(value);
    }
    else
    {
        m_stackData[currentWrite & m_bitMask] = std::move(value);
    }

    // Advance write count - this automatically handles overwriting
    m_writeIndex.store(nextWrite, std::memory_order::release);
    m_cachedWrite = nextWrite;

    return true;
}

template<typename T, size_t SIZE>
bool
LockFreeFIFO<T, SIZE>::pushElement(const T& value) noexcept
{
    auto currentRead        = m_readIndex.load(std::memory_order::acquire);
    const auto currentWrite = m_cachedWrite;
    const auto nextWrite    = currentWrite + 1;

    if ((nextWrite & m_bitMask) == (currentRead & m_bitMask))
    {
        return false;
    }

    // Store the new element
    if constexpr (SIZE == 0)
    {
        m_data[currentWrite & m_bitMask] = value;
    }
    else
    {
        m_stackData[currentWrite & m_bitMask] = value;
    }

    // Advance write count - this automatically handles overwriting
    m_writeIndex.store(nextWrite, std::memory_order::release);
    m_cachedWrite = nextWrite;

    return true;
}

template<typename T, size_t SIZE>
size_t
LockFreeFIFO<T, SIZE>::popElements(T* dst, size_t numValues) noexcept
    requires std::is_trivially_copyable_v<T>
{
    const auto readIndex = m_cachedRead;

    const auto writeIndex = m_writeIndex.load(std::memory_order::acquire);

    const size_t numValsToPop =
      std::min(writeIndex - readIndex & m_bitMask, numValues);

    size_t size1 = std::min(getCapacity() - readIndex, numValsToPop);
    size_t size2 = 0;

    if (size1 != numValsToPop)
    {
        size2 = numValsToPop - size1;
    }

    if constexpr (SIZE == 0)
    {
        std::memcpy(dst, m_data.data() + readIndex, size1 * sizeof(T));

        if (size2)
        {
            std::memcpy(dst + size1, m_data.data(), size2 * sizeof(T));
        }
    }
    else
    {
        std::memcpy(dst, m_stackData.data() + readIndex, size1 * sizeof(T));

        if (size2)
        {
            std::memcpy(dst + size1, m_stackData.data(), size2 * sizeof(T));
        }
    }

    const auto nextRead = readIndex + numValsToPop & m_bitMask;
    m_readIndex.store(nextRead, std::memory_order::release);
    m_cachedRead = nextRead;

    return numValsToPop;
}

template<typename T, size_t SIZE>
size_t
LockFreeFIFO<T, SIZE>::pushElements(const T* values, size_t numValues) noexcept
    requires std::is_trivially_copyable_v<T>
{
    // jassert(numValues < SIZE - 1, "Too many values, for this fifos
    // capacity!");

    auto readIndex  = m_readIndex.load(std::memory_order::acquire);
    auto writeIndex = m_cachedWrite;

    numValues = std::min(numValues, (readIndex - writeIndex & m_bitMask) - 1);

    if (numValues == 0)
    {
        return 0;
    }

    size_t size1 = std::min(numValues, getCapacity() - writeIndex);
    size_t size2 = numValues - size1;

    if constexpr (SIZE == 0)
    {
        std::memcpy(m_data.data() + writeIndex, values, sizeof(T) * size1);

        if (size2)
        {
            std::memcpy(m_data.data(), values + size1, sizeof(T) * size2);
        }
    }
    else
    {
        std::memcpy(m_stackData.data() + writeIndex, values, sizeof(T) * size1);

        if (size2)
        {
            std::memcpy(m_stackData.data(), values + size1, sizeof(T) * size2);
        }
    }

    const auto nextWrite = m_writeIndex + numValues & m_bitMask;
    m_cachedWrite        = nextWrite;
    m_writeIndex.store(nextWrite, std::memory_order::release);

    return numValues;
}

template<typename T, size_t SIZE>
size_t
LockFreeFIFO<T, SIZE>::getSize() const noexcept
{
    const auto readCount  = m_readIndex.load(std::memory_order::relaxed);
    const auto writeCount = m_writeIndex.load(std::memory_order::relaxed);
    return writeCount - readCount & m_bitMask;
}

template<typename T, size_t SIZE>
size_t
LockFreeFIFO<T, SIZE>::getSpaceAvailable() const noexcept
{
    return SIZE - getSize() - 1;
}

template<typename T, size_t SIZE>
size_t
LockFreeFIFO<T, SIZE>::getCapacity() const noexcept
{
    return SIZE > 0 ? SIZE - 1 : m_data.size() - 1;
}
}

struct PluginUtilities
{
    PluginUtilities() = delete;

    static int getDesktopFlags(const AudioProcessorEditor& editor)
    {
        return editor.wantsLayerBackedView()
                 ? 0
                 : ComponentPeer::
                     windowRequiresSynchronousCoreGraphicsRendering;
    }

    static int getDesktopFlags(const AudioProcessorEditor* editor)
    {
        return editor != nullptr ? getDesktopFlags(*editor) : 0;
    }

    static void addToDesktop(AudioProcessorEditor& editor, void* parent)
    {
        editor.addToDesktop(getDesktopFlags(editor), parent);
    }

    static const PluginHostType& getHostType()
    {
        static PluginHostType hostType;
        return hostType;
    }

#ifndef JUCE_VST3_CAN_REPLACE_VST2
#define JUCE_VST3_CAN_REPLACE_VST2 1
#endif

    // NB: Nasty old-fashioned code in here because it's copied from the
    // Steinberg example code.
    static void getUUIDForVST2ID(bool forControllerUID, uint8 uuid[16])
    {
#if JUCE_WINDOWS && !JUCE_MINGW
        const auto juce_sprintf = [](auto&& head, auto&&... tail)
        { sprintf_s(head, (size_t)numElementsInArray(head), tail...); };
        const auto juce_strcpy = [](auto&& head, auto&&... tail)
        { strcpy_s(head, (size_t)numElementsInArray(head), tail...); };
        const auto juce_strcat = [](auto&& head, auto&&... tail)
        { strcat_s(head, (size_t)numElementsInArray(head), tail...); };
        const auto juce_sscanf = [](auto&&... args) { sscanf_s(args...); };
#else
        const auto juce_sprintf = [](auto&& head, auto&&... tail)
        { snprintf(head, (size_t)numElementsInArray(head), tail...); };
        const auto juce_strcpy = [](auto&&... args) { strcpy(args...); };
        const auto juce_strcat = [](auto&&... args) { strcat(args...); };
        const auto juce_sscanf = [](auto&&... args) { sscanf(args...); };
#endif

        char uidString[33];

        const int vstfxid =
          (('V' << 16) | ('S' << 8) | (forControllerUID ? 'E' : 'T'));
        char vstfxidStr[7] = { 0 };
        juce_sprintf(vstfxidStr, "%06X", vstfxid);

        juce_strcpy(uidString, vstfxidStr);

        char uidStr[9] = { 0 };
        juce_sprintf(uidStr, "%08X", JucePlugin_VSTUniqueID);
        juce_strcat(uidString, uidStr);

        char nameidStr[3] = { 0 };
        const size_t len  = strlen(JucePlugin_Name);

        for (size_t i = 0; i <= 8; ++i)
        {
            juce::uint8 c =
              i < len ? static_cast<juce::uint8>(JucePlugin_Name[i]) : 0;

            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';

            juce_sprintf(nameidStr, "%02X", c);
            juce_strcat(uidString, nameidStr);
        }

        unsigned long p0;
        unsigned int p1, p2;
        unsigned int p3[8];

        juce_sscanf(uidString,
                    "%08lX%04X%04X%02X%02X%02X%02X%02X%02X%02X%02X",
                    &p0,
                    &p1,
                    &p2,
                    &p3[0],
                    &p3[1],
                    &p3[2],
                    &p3[3],
                    &p3[4],
                    &p3[5],
                    &p3[6],
                    &p3[7]);

        union q0_u
        {
            uint32 word;
            uint8 bytes[4];
        } q0;

        union q1_u
        {
            uint16 half;
            uint8 bytes[2];
        } q1, q2;

        q0.word = static_cast<uint32>(p0);
        q1.half = static_cast<uint16>(p1);
        q2.half = static_cast<uint16>(p2);

        // VST3 doesn't use COM compatible UUIDs on non windows platforms
#if !JUCE_WINDOWS
        q0.word = ByteOrder::swap(q0.word);
        q1.half = ByteOrder::swap(q1.half);
        q2.half = ByteOrder::swap(q2.half);
#endif

        for (int i = 0; i < 4; ++i)
            uuid[i + 0] = q0.bytes[i];

        for (int i = 0; i < 2; ++i)
            uuid[i + 4] = q1.bytes[i];

        for (int i = 0; i < 2; ++i)
            uuid[i + 6] = q2.bytes[i];

        for (int i = 0; i < 8; ++i)
            uuid[i + 8] = static_cast<uint8>(p3[i]);
    }

#if JucePlugin_Build_VST
    static bool handleManufacturerSpecificVST2Opcode(
      [[maybe_unused]] int32 index,
      [[maybe_unused]] pointer_sized_int value,
      [[maybe_unused]] void* ptr,
      float)
    {
#if JUCE_VST3_CAN_REPLACE_VST2
        if ((index == (int32)ByteOrder::bigEndianInt("stCA") ||
             index == (int32)ByteOrder::bigEndianInt("stCa")) &&
            value == (int32)ByteOrder::bigEndianInt("FUID") && ptr != nullptr)
        {
            uint8 fuid[16];
            getUUIDForVST2ID(false, fuid);
            ::memcpy(ptr, fuid, 16);
            return true;
        }
#endif
        return false;
    }
#endif
};

} // namespace juce::detail
