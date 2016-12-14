#ifndef __EscargotArrayObject__
#define __EscargotArrayObject__

#include "runtime/Object.h"
#include "runtime/ErrorObject.h"

namespace Escargot {

#define ESCARGOT_ARRAY_NON_FASTMODE_MIN_SIZE 65536 * 2
#define ESCARGOT_ARRAY_NON_FASTMODE_START_MIN_GAP 1024

class ArrayObject : public Object {
    friend class Context;
    friend class ByteCodeInterpreter;

public:
    ArrayObject(ExecutionState& state);
    virtual bool isArrayObject()
    {
        return true;
    }


    uint32_t getLength(ExecutionState& state)
    {
        if (LIKELY(isPlainObject())) {
            return m_values[1].toUint32(state);
        } else {
            return getLengthSlowCase(state);
        }
    }

    void setLength(ExecutionState& state, const uint32_t& value)
    {
        setArrayLength(state, value);
        if (LIKELY(isPlainObject())) {
            m_values[1] = SmallValue(value);
        } else {
            setLengthSlowCase(state, Value(value));
        }
    }

    virtual ObjectGetResult getOwnProperty(ExecutionState& state, const ObjectPropertyName& P) ESCARGOT_OBJECT_SUBCLASS_MUST_REDEFINE;
    virtual bool defineOwnProperty(ExecutionState& state, const ObjectPropertyName& P, const ObjectPropertyDescriptorForDefineOwnProperty& desc) ESCARGOT_OBJECT_SUBCLASS_MUST_REDEFINE;
    virtual void deleteOwnProperty(ExecutionState& state, const ObjectPropertyName& P) ESCARGOT_OBJECT_SUBCLASS_MUST_REDEFINE;
    virtual void enumeration(ExecutionState& state, std::function<bool(const ObjectPropertyName&, const ObjectPropertyDescriptor& desc)> callback) ESCARGOT_OBJECT_SUBCLASS_MUST_REDEFINE;
    virtual uint32_t length(ExecutionState& state)
    {
        return getLength(state);
    }

protected:
    Value getLengthSlowCase(ExecutionState& state);
    bool setLengthSlowCase(ExecutionState& state, const Value& value);

    bool isFastModeArray()
    {
        if (LIKELY(m_rareData == nullptr)) {
            return true;
        }
        return m_rareData->m_isFastModeArrayObject;
    }

    // return values means state of isFastMode
    bool setArrayLength(ExecutionState& state, const uint32_t& newLength)
    {
        if (UNLIKELY(newLength == Value::InvalidArrayIndexValue)) {
            ErrorObject::throwBuiltinError(state, ErrorObject::Code::RangeError, errorMessage_GlobalObject_InvalidArrayLength);
        }

        if (UNLIKELY(newLength > ESCARGOT_ARRAY_NON_FASTMODE_MIN_SIZE)) {
            if (isFastModeArray()) {
                uint32_t orgLength = getLength(state);
                if (newLength > orgLength) {
                    if (newLength - orgLength > ESCARGOT_ARRAY_NON_FASTMODE_START_MIN_GAP) {
                        convertIntoNonFastMode();
                    }
                }
            }
        }

        if (LIKELY(isFastModeArray())) {
            m_values[1] = Value(newLength);
            m_fastModeData.resize(newLength, Value(Value::EmptyValue));
            return true;
        } else {
            RELEASE_ASSERT_NOT_REACHED();
            return false;
        }
    }

    void convertIntoNonFastMode()
    {
        // TODO
        RELEASE_ASSERT_NOT_REACHED();
    }

    ALWAYS_INLINE ObjectGetResult getFastModeValue(ExecutionState& state, const ObjectPropertyName& P)
    {
        if (LIKELY(isFastModeArray())) {
            uint32_t idx;
            if (LIKELY(P.isUIntType())) {
                idx = P.uintValue();
            } else {
                idx = P.string(state)->tryToUseAsArrayIndex();
            }
            if (LIKELY(idx != Value::InvalidArrayIndexValue)) {
                ASSERT(m_fastModeData.size() == getLength(state));
                if (LIKELY(idx < m_fastModeData.size())) {
                    Value v = m_fastModeData[idx];
                    if (LIKELY(!v.isEmpty())) {
                        return ObjectGetResult(v, true, true, true);
                    }
                    return ObjectGetResult();
                }
            }
        }
        return ObjectGetResult();
    }

    ALWAYS_INLINE bool setFastModeValue(ExecutionState& state, const ObjectPropertyName& P, const ObjectPropertyDescriptorForDefineOwnProperty& desc)
    {
        if (LIKELY(isFastModeArray())) {
            uint32_t idx;
            if (LIKELY(P.isUIntType())) {
                idx = P.uintValue();
            } else {
                idx = P.string(state)->tryToUseAsArrayIndex();
            }
            if (LIKELY(idx != Value::InvalidArrayIndexValue)) {
                if (UNLIKELY(!desc.isDataWritableEnumerableConfigurable())) {
                    convertIntoNonFastMode();
                    return false;
                }
                uint32_t len = m_fastModeData.size();
                if (UNLIKELY(len <= idx)) {
                    if (UNLIKELY(!setArrayLength(state, idx + 1))) {
                        return false;
                    }
                }
                ASSERT(m_fastModeData.size() == getLength(state));
                m_fastModeData[idx] = desc.value();
                return true;
            }
        }
        return false;
    }

    Vector<Value, gc_malloc_ignore_off_page_allocator<Value>> m_fastModeData;
};
}

#endif
