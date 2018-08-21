//
//  Copyright 2018 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "Forward.h"

#include "ScriptableMesh.h"
#include "ScriptableMeshPart.h"

#include "GraphicsScriptingInterface.h"
#include "GraphicsScriptingUtil.h"
#include "OBJWriter.h"
#include <BaseScriptEngine.h>
#include <QtScript/QScriptValue>
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/transform.hpp>
#include <graphics/BufferViewHelpers.h>
#include <graphics/GpuHelpers.h>
#include <graphics/Geometry.h>
#include <shared/Scriptable.h>

namespace {
    Extents getBounds(const graphics::MeshPointer& mesh) {
        Extents extents;
        auto view = mesh->getVertexBuffer();
        std::for_each(view.cbegin<glm::vec3>(), view.cend<glm::vec3>(), [&](const glm::vec3& pos) { extents.addPoint(pos); });
        return extents;
    }
    glm::vec3 getCenterPoint(const graphics::MeshPointer& mesh) {
        Extents extents = getBounds(mesh);
        return (extents.maximum + extents.minimum) / 2.0f;
    }
}

QString scriptable::ScriptableMesh::toString() const {
    if (isValid()) {
        const QString TOSTRING_TEMPLATE{ "[ScriptableMesh%1 numParts=%2 numVertices=%3]" };
        auto objectName = QString::fromStdString(getNativeObject()->displayName);
        auto name = objectName.isEmpty() ? "" : " name=" + objectName;
        return TOSTRING_TEMPLATE.arg(name).arg(getNumParts()).arg(getNumVertices());
    }
    return "[ScriptableMesh nullptr]";
}

QVariant scriptable::ScriptableMesh::toVariant() const {
    return QVariant::fromValue<scriptable::MappedQObject>({ this, &staticMetaObject });
}

QVector<scriptable::ScriptableMeshPartPointer> scriptable::ScriptableMesh::getMeshParts() const {
    QVector<scriptable::ScriptableMeshPartPointer> out(getNumParts());
    for (glm::uint32 i = 0; i < getNumParts(); i++) {
        out[i] = scriptable::ScriptableMeshPartPointer::create(getMeshPointer(), i);
    }
    return out;
}

glm::uint32 scriptable::ScriptableMesh::getNumIndices() const {
    if (auto mesh = getMeshPointer()) {
        return (glm::uint32)mesh->getNumIndices();
    }
    return 0;
}

glm::uint32 scriptable::ScriptableMesh::getNumVertices() const {
    if (auto mesh = getMeshPointer()) {
        return (glm::uint32)mesh->getNumVertices();
    }
    return 0;
}

std::vector<glm::uint32> scriptable::ScriptableMesh::getIndices() const {
    if (auto mesh = getMeshPointer()) {
        auto indices = mesh->getIndexBuffer();
        if (indices._element.getType() == gpu::UINT16) {
            return buffer_helpers::coerceVector<glm::uint32>(buffer_helpers::bufferToVector<glm::uint16>(indices));
        } else {
            return buffer_helpers::bufferToVector<glm::uint32>(indices);
        }
    }
    return std::vector<glm::uint32>();
}

glm::uint32 scriptable::ScriptableMesh::getNumAttributes() const {
    if (auto mesh = getMeshPointer()) {
        return (glm::uint32)mesh->getNumAttributes() + 1;
    }
    return 0;
}
std::vector<std::string> scriptable::ScriptableMesh::getAttributeNames() const {
    std::vector<std::string> result;
    if (auto mesh = getMeshPointer()) {
        for (const auto& a : buffer_helpers::ATTRIBUTES.toStdMap()) {
            auto bufferView = buffer_helpers::mesh::getBufferView(mesh, a.second);
            if (bufferView.getNumElements() > 0) {
                result.emplace_back(a.first.toStdString());
            }
        }
    }
    return result;
}

QVariantMap scriptable::ScriptableMesh::getVertexAttributes(glm::uint32 vertexIndex) const {
    if (!isValidIndex(vertexIndex)) {
        return QVariantMap();
    }
    return buffer_helpers::mesh::getVertexAttributes(getMeshPointer(), vertexIndex).toMap();
}

bool scriptable::ScriptableMesh::setVertexAttributes(glm::uint32 vertexIndex, const QVariantMap& attributes) {
    QVariantMap normalized;
    for (const auto& name : attributes.keys()) {
        QString normalizedName = scriptable::JSVectorAdapter::singularizeAttributeName(name);
        if (!isValidIndex(vertexIndex, normalizedName)) {
            return false;
        }
        normalized[normalizedName] = attributes[name];
    }
    return buffer_helpers::mesh::setVertexAttributes(getMeshPointer(), vertexIndex, normalized);
}

int scriptable::ScriptableMesh::getSlotNumber(const QString& attributeName) const {
    if (auto mesh = getMeshPointer()) {
        QString normalizedName = scriptable::JSVectorAdapter::singularizeAttributeName(attributeName);
        return buffer_helpers::ATTRIBUTES.value(normalizedName, -1);
    }
    return -1;
}

QVariantMap scriptable::ScriptableMesh::getBufferFormats() const {
    auto mesh = getMeshPointer();
    if (!mesh) {
        return QVariantMap();
    }
    auto bufferView = mesh->getIndexBuffer();
    QVariantMap result = {{ "index", QVariant::fromValue(bufferView) }};

    for (const auto& a : buffer_helpers::ATTRIBUTES.toStdMap()) {
        auto bufferView = buffer_helpers::mesh::getBufferView(mesh, a.second);
        if (bufferView.getNumElements() > 0) {
            auto map = QVariant::fromValue(bufferView).toMap();
            map["slot"] = a.second;
            result[a.first] = map;
        }
    }
    return result;
}

bool scriptable::ScriptableMesh::removeAttribute(const QString& attributeName) {
    auto slot = isValid() ? getSlotNumber(attributeName) : -1;
    if (slot < 0) {
        return false;
    }
    if (slot == gpu::Stream::POSITION) {
        context()->throwError("cannot remove .position attribute");
        return false;
    }
    if (buffer_helpers::mesh::getBufferView(getMeshPointer(), slot).getNumElements()) {
        getMeshPointer()->removeAttribute(slot);
        return true;
    }
    return false;
}

glm::uint32 scriptable::ScriptableMesh::addAttribute(const QString& attributeName, const QVariant& defaultValue) {
    auto slot = isValid() ? getSlotNumber(attributeName) : -1;
    if (slot < 0) {
        return 0;
    }
    auto mesh = getMeshPointer();
    auto numVertices = getNumVertices();
    auto names = getAttributeNames();
    if (std::find(names.begin(), names.end(), attributeName.toStdString()) == names.end()) {
        std::vector<QVariant> values(numVertices, defaultValue);
        mesh->addAttribute(slot, buffer_helpers::newFromVector(values, gpu::Stream::getDefaultElements()[slot]));
        return (glm::uint32)values.size();
    } else {
        auto bufferView = buffer_helpers::mesh::getBufferView(mesh, slot);
        auto current = (glm::uint32)bufferView.getNumElements();
        if (current < numVertices) {
            bufferView = buffer_helpers::resized(bufferView, numVertices);
            for (glm::uint32 i = current; i < numVertices; i++) {
                buffer_helpers::setValue<QVariant>(bufferView, i, defaultValue);
            }
            return numVertices - current;
        } else if (current > numVertices) {
            qCDebug(graphics_scripting) << QString("addAttribute current=%1 > numVertices=%2").arg(current).arg(numVertices);
            return 0;
        }
    }
    return 0;
}

glm::uint32 scriptable::ScriptableMesh::fillAttribute(const QString& attributeName, const QVariant& value) {
    auto slot = isValid() ? getSlotNumber(attributeName) : -1;
    if (slot < 0) {
        return 0;
    }
    auto mesh = getMeshPointer();
    auto numVertices = getNumVertices();
    std::vector<QVariant> values(numVertices, value);
    mesh->addAttribute(slot, buffer_helpers::newFromVector(values, gpu::Stream::getDefaultElements()[slot]));
    return true;
}

Extents scriptable::ScriptableMesh::getMeshExtents() const {
    if (auto mesh = getMeshPointer()) {
        return getBounds(mesh);
    }
    return Extents();
}

glm::uint32 scriptable::ScriptableMesh::getNumParts() const {
    if (auto mesh = getMeshPointer()) {
        return (glm::uint32)mesh->getNumParts();
    }
    return 0;
}

QVariantList scriptable::ScriptableMesh::queryVertexAttributes(QVariant selector) const {
    QVariantList result;
    const auto& attributeName = selector.toString();
    if (!isValidIndex(0, attributeName)) {
        return result;
    }
    auto slotNum = getSlotNumber(attributeName);
    const auto& bufferView = buffer_helpers::mesh::getBufferView(getMeshPointer(), static_cast<gpu::Stream::Slot>(slotNum));
    glm::uint32 numElements = (glm::uint32)bufferView.getNumElements();
    const std::string debugHint = attributeName.toStdString();
    for (glm::uint32 i = 0; i < numElements; i++) {
        result << buffer_helpers::getValue<QVariant>(bufferView, i, debugHint.c_str());
    }
    return result;
}

QVariant scriptable::ScriptableMesh::getVertexProperty(glm::uint32 vertexIndex, const QString& attributeName) const {
    if (!isValidIndex(vertexIndex, attributeName)) {
        return QVariant();
    }
    auto slotNum = getSlotNumber(attributeName);
    const auto& bufferView = buffer_helpers::mesh::getBufferView(getMeshPointer(), static_cast<gpu::Stream::Slot>(slotNum));
    return buffer_helpers::getValue<QVariant>(bufferView, vertexIndex, qUtf8Printable(attributeName));
}

bool scriptable::ScriptableMesh::setVertexProperty(glm::uint32 vertexIndex, const QString& attributeName, const QVariant& value) {
    if (!isValidIndex(vertexIndex, attributeName)) {
        return false;
    }
    auto slotNum = getSlotNumber(attributeName);
    const auto& bufferView = buffer_helpers::mesh::getBufferView(getMeshPointer(), static_cast<gpu::Stream::Slot>(slotNum));
    return buffer_helpers::setValue(bufferView, vertexIndex, value);
}

glm::uint32 scriptable::ScriptableMesh::forEachVertex(QScriptValue _callback) {
    auto mesh = getMeshPointer();
    if (!mesh) {
        return 0;
    }
    auto scopedHandler = jsBindCallback(_callback);

    // destructure so we can still invoke callback scoped, but with a custom signature (obj, i, jsMesh)
    auto scope = scopedHandler.property("scope");
    auto callback = scopedHandler.property("callback");
    auto js = engine() ? engine() : scopedHandler.engine(); // cache value to avoid resolving each iteration
    if (!js) {
        return 0;
    }
    auto meshObject = js ? js->toScriptValue(getNativeObject()) : QScriptValue::NullValue;
    int numProcessed = 0;
    buffer_helpers::mesh::forEachVertex(mesh, [&](glm::uint32 index, const QVariantMap& values) {
        auto result = callback.call(scope, { js->toScriptValue(values), index, meshObject });
        if (js->hasUncaughtException()) {
            js->currentContext()->throwValue(js->uncaughtException());
            return false;
        }
        numProcessed++;
        return true;
    });
    return numProcessed;
}


glm::uint32 scriptable::ScriptableMesh::updateVertexAttributes(QScriptValue _callback) {
    auto mesh = getMeshPointer();
    if (!mesh) {
        return 0;
    }
    auto scopedHandler = jsBindCallback(_callback);

    // destructure so we can still invoke callback scoped, but with a custom signature (obj, i, jsMesh)
    auto scope = scopedHandler.property("scope");
    auto callback = scopedHandler.property("callback");
    auto js = engine() ? engine() : scopedHandler.engine(); // cache value to avoid resolving each iteration
    if (!js) {
        return 0;
    }
    auto meshObject = js ? js->toScriptValue(getNativeObject()) : QScriptValue::NullValue;
    int numProcessed = 0;
    auto attributeViews = buffer_helpers::mesh::getAllBufferViews(mesh);
    buffer_helpers::mesh::forEachVertex(mesh, [&](glm::uint32 index, const QVariantMap& values) {
        auto obj = js->toScriptValue(values);
        auto result = callback.call(scope, { obj, index, meshObject });
        if (js->hasUncaughtException()) {
            js->currentContext()->throwValue(js->uncaughtException());
            return false;
        }
        if (result.isBool() && !result.toBool()) {
            // bail without modifying data if user explicitly returns false
            return true;
        }
        if (result.isObject() && !result.strictlyEquals(obj)) {
            // user returned a new object (ie: instead of modifying input properties)
            obj = result;
        }
        for (const auto& a : attributeViews) {
            const auto& attribute = obj.property(a.first);
            if (attribute.isValid()) {
                buffer_helpers::setValue(a.second, index, attribute.toVariant());
            }
        }
        numProcessed++;
        return true;
    });
    return numProcessed;
}

// protect against user scripts sending bogus values
bool scriptable::ScriptableMesh::isValidIndex(glm::uint32 vertexIndex, const QString& attributeName) const {
    if (!isValid()) {
        return false;
    }
    const auto last = getNumVertices() - 1;
    if (vertexIndex > last) {
        if (context()) {
            context()->throwError(QString("vertexIndex=%1 out of range (firstVertexIndex=%2, lastVertexIndex=%3)").arg(vertexIndex).arg(0).arg(last));
        }
        return false;
    }
    if (!attributeName.isEmpty()) {
        auto slotNum = getSlotNumber(attributeName);
        if (slotNum < 0) {
            if (context()) {
                context()->throwError(QString("invalid attributeName=%1").arg(attributeName));
            }
            return false;
        }
        auto view = buffer_helpers::mesh::getBufferView(getMeshPointer(), static_cast<gpu::Stream::Slot>(slotNum));
        if (vertexIndex >= (glm::uint32)view.getNumElements()) {
            if (context()) {
                context()->throwError(QString("vertexIndex=%1 out of range (attribute=%2, numElements=%3)").arg(vertexIndex).arg(attributeName).arg(view.getNumElements()));
            }
            return false;
        }
    }
    return true;
}

scriptable::ScriptableMeshPointer scriptable::ScriptableMesh::recenter(const glm::vec3& origin) {
    if (auto mesh = getMeshPointer()) {
        glm::vec3 zero = glm::isnan(origin.x) ? glm::vec3() : origin;
        return translate(zero - getCenterPoint(mesh));
    }
    return nullptr;
}

scriptable::ScriptableMeshPointer scriptable::ScriptableMesh::scaleToFit(float unitScale) {
    if (auto mesh = getMeshPointer()) {
        float maxDimension = getBounds(mesh).largestDimension();
        return scale(glm::vec3(unitScale / maxDimension), getCenterPoint(mesh));
    }
    return {};
}
scriptable::ScriptableMeshPointer scriptable::ScriptableMesh::translate(const glm::vec3& translation) {
    return transform(glm::translate(translation));
}
scriptable::ScriptableMeshPointer scriptable::ScriptableMesh::scale(const glm::vec3& inputScale, const glm::vec3& origin) {
    glm::vec3 scale = inputScale;
    if (!glm::length(scale) && context()) {
        scale = glm::vec3(context()->argument(0).toNumber());
    }
    if (!jsAssert(glm::length(scale), "scale: expected non-zero scaling factor")) {
        return nullptr;
    }
    if (auto mesh = getMeshPointer()) {
        glm::vec3 offset = glm::isnan(origin.x) ? getCenterPoint(mesh) : origin;
        return transform(glm::translate(-offset) * glm::scale(scale) * glm::translate(offset));
    }
    return {};
}
scriptable::ScriptableMeshPointer scriptable::ScriptableMesh::rotateVec3Degrees(const glm::vec3& eulerAngles, const glm::vec3& origin) {
    return rotate(glm::quat(glm::radians(eulerAngles)), origin);
}
scriptable::ScriptableMeshPointer scriptable::ScriptableMesh::rotateDegrees(float x, float y, float z, const glm::vec3& origin) {
    return rotateVec3Degrees({ x, y, z }, origin);
}
scriptable::ScriptableMeshPointer scriptable::ScriptableMesh::rotate(const glm::quat& rotation, const glm::vec3& origin) {
    if (auto mesh = getMeshPointer()) {
        glm::vec3 offset = glm::isnan(origin.x) ? getCenterPoint(mesh) : origin;
        return transform(glm::translate(-offset) * glm::toMat4(rotation) * glm::translate(offset));
    }
    return {};
}
scriptable::ScriptableMeshPointer scriptable::ScriptableMesh::transform(const glm::mat4& transform) {
    if (auto mesh = getMeshPointer()) {
        float angle = glm::angle(glm::quat(transform));
        buffer_helpers::mesh::transformVec3Buffer(mesh, gpu::Stream::POSITION, transform);
        if (!glm::isnan(angle) && angle != 0.0f) {
            buffer_helpers::mesh::transformVec3Buffer(mesh, gpu::Stream::NORMAL, transform, true);
        }
        return getNativeObject();
    }
    return {};
}

scriptable::ScriptableMeshPointer scriptable::ScriptableMesh::cloneMesh() const {
    auto mesh = jsAssert(getMeshPointer(), "cloneMesh: null mesh");
    return mesh ? buffer_helpers::mesh::clone(mesh) : nullptr;
}

QString scriptable::ScriptableMesh::toOBJ() {
    auto mesh = jsAssert(getMeshPointer(), "toOBJ: null mesh");
    return mesh ? writeOBJToString({ mesh }) : QString();
}

scriptable::ScriptableMeshPointer scriptable::ScriptableMesh::dedupeVertices(float epsilon, bool resetNormals) {
    const auto mesh = jsAssert(getMeshPointer(), "dedupeVertices on null mesh");
    jsThrowError(engine(), "TODO: dedupeVertices moved to separate PR");
    return nullptr;
}
