#pragma once

#include "../VisualControls.h"
#include <cassert>
#include <string>
#include <unordered_map>

struct ParamBinding {
    enum class Type { Float, Int, Bool, Vec3, Vec4 };

    using FloatAccessor = float& (*)(VisualControls&);
    using IntAccessor = int& (*)(VisualControls&);
    using BoolAccessor = bool& (*)(VisualControls&);
    using Vec3Accessor = glm::vec3& (*)(VisualControls&);
    using Vec4Accessor = glm::vec4& (*)(VisualControls&);

    Type type;
    FloatAccessor getFloat = nullptr;
    IntAccessor getInt = nullptr;
    BoolAccessor getBool = nullptr;
    Vec3Accessor getVec3 = nullptr;
    Vec4Accessor getVec4 = nullptr;

    float& accessFloat(VisualControls& c) const {
        assert(type == Type::Float && getFloat);
        return getFloat(c);
    }

    int& accessInt(VisualControls& c) const {
        assert(type == Type::Int && getInt);
        return getInt(c);
    }

    bool& accessBool(VisualControls& c) const {
        assert(type == Type::Bool && getBool);
        return getBool(c);
    }

    glm::vec3& accessVec3(VisualControls& c) const {
        assert(type == Type::Vec3 && getVec3);
        return getVec3(c);
    }

    glm::vec4& accessVec4(VisualControls& c) const {
        assert(type == Type::Vec4 && getVec4);
        return getVec4(c);
    }

    void set(VisualControls& c, float value) const {
        accessFloat(c) = value;
    }

    void set(VisualControls& c, int value) const {
        accessInt(c) = value;
    }

    void set(VisualControls& c, bool value) const {
        accessBool(c) = value;
    }
};

class ParameterBindingRegistry {
public:
    static const std::unordered_map<std::string, ParamBinding>& get();
};
