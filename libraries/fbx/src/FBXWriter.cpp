//
//  FBXWriter.cpp
//  libraries/fbx/src
//
//  Created by Ryan Huffman on 9/5/17.
//  Copyright 2017 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "FBXWriter.h"

#include <QDebug>


QByteArray FBXWriter::encodeFBX(const FBXNode& root) {
    QByteArray data;
    QDataStream out(&data, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setVersion(QDataStream::Qt_4_5);

    out.writeRawData(FBX_BINARY_PROLOG, FBX_BINARY_PROLOG.size());
    auto bytes = QByteArray(FBX_HEADER_BYTES_BEFORE_VERSION - FBX_BINARY_PROLOG.size(), '\0');
    out.writeRawData(bytes, bytes.size());

    out << FBX_VERSION_2016;

    for (auto& child : root.children) {
        encodeNode(out, child);
    }
    encodeNode(out, FBXNode());

    return data;
}

void FBXWriter::encodeNode(QDataStream& out, const FBXNode& node) {
    qDebug() << "Encoding " << node.name;

    auto device = out.device();
    auto nodeStartPos = device->pos();

    // endOffset (temporary, updated later)
    out << (qint64)0;

    // Property count
    out << (quint64)node.properties.size();

    // Property list length (temporary, updated later)
    out << (quint64)0;

    out << (quint8)node.name.size();
    out.writeRawData(node.name, node.name.size());

    if (node.name == "Vertices") {
        for (auto& prop : node.properties) {
            qDebug() << "Properties: " << prop;
        }
    }

    auto nodePropertiesStartPos = device->pos();

    for (const auto& prop : node.properties) {
        encodeFBXProperty(out, prop);
    }

    // Go back and write property list length
    auto nodePropertiesEndPos = device->pos();
    device->seek(nodeStartPos + sizeof(qint64) + sizeof(quint64));
    out << (quint64)(nodePropertiesEndPos - nodePropertiesStartPos);

    device->seek(nodePropertiesEndPos);

    for (auto& child : node.children) {
        encodeNode(out, child);
    }

    if (node.children.length() > 0) {
        encodeNode(out, FBXNode());
    }

    // Go back and write actual endOffset
    auto nodeEndPos = device->pos();
    device->seek(nodeStartPos);
    out << (qint64)(nodeEndPos);

    device->seek(nodeEndPos);
}

void FBXWriter::encodeFBXProperty(QDataStream& out, const QVariant& prop) {
    auto type = prop.userType();
    switch (type) {
        case QVariant::Type::Bool:

            out.device()->write("C", 1);
            out << prop.toBool();
            break;

        case QMetaType::Int:
            out.device()->write("I", 1);
            out << prop.toInt();
            break;

        encodeNode(out, FBXNode());
        case QMetaType::Float:
            out.device()->write("F", 1);
            out << prop.toFloat();
            break;

        case QMetaType::Double:
            out.device()->write("D", 1);
            out << prop.toDouble();
            break;

        case QMetaType::LongLong:
            out.device()->write("L", 1);
            out << prop.toLongLong();
            break;

        case QMetaType::QString:
        {
            auto& bytes = prop.toString().toUtf8();
            out << 'S';
            out << bytes.length();
            out << bytes;
            out << (int32_t)bytes.size();
            out.writeRawData(bytes, bytes.size());
            break;
        }

        case QMetaType::QByteArray:
            {
                auto& bytes = prop.toByteArray();
                out.device()->write("S", 1);
                out << (int32_t)bytes.size();
                out.writeRawData(bytes, bytes.size());
                break;
            }

            // TODO Delete? Do we ever use QList instead of QVector?
        case QVariant::Type::List:
            {
                auto& list = prop.toList();
                auto listType = prop.userType();

                switch (listType) {
                    case QMetaType::Float:
                        out.device()->write("f", 1);
                        out << (int32_t)list.length();
                        out << (int32_t)0;
                        out << (int32_t)0;
                        for (auto& innerProp : list) {
                            out << prop.toFloat();
                        }
                        break;

                    case QMetaType::Double:
                        out.device()->write("d", 1);
                        out << (int32_t)list.length();
                        out << (int32_t)0;
                        out << (int32_t)0;
                        for (auto& innerProp : list) {
                            out << prop.toDouble();
                        }
                        break;

                    case QMetaType::LongLong:
                        out.device()->write("l", 1);
                        out << (int32_t)list.length();
                        out << (int32_t)0;
                        out << (int32_t)0;
                        for (auto& innerProp : list) {
                            out << prop.toLongLong();
                        }
                        break;

                    case QMetaType::Int:
                        out.device()->write("i", 1);
                        out << (int32_t)list.length();
                        out << (int32_t)0;
                        out << (int32_t)0;
                        for (auto& innerProp : list) {
                            out << prop.toInt();
                        }
                        break;

                    case QMetaType::Bool:
                        out.device()->write("b", 1);
                        out << (int32_t)list.length();
                        out << (int32_t)0;
                        out << (int32_t)0;
                        for (auto& innerProp : list) {
                            out << prop.toBool();
                        }
                        break;
                }
            }
            break;

        default:
        {
            if (prop.canConvert<QVector<float>>()) {
                auto list = prop.value<QVector<float>>();
                out.device()->write("f", 1);
                out << (int32_t)list.length();
                out << (int32_t)0;
                out << (int32_t)0;
                for (auto& value : list) {
                    out << value;
                }
            } else if (prop.canConvert<QVector<double>>()) {
                auto list = prop.value<QVector<double>>();
                out.device()->write("d", 1);
                out << (int32_t)list.length();
                out << (int32_t)0;
                out << (int32_t)0;
                for (auto& value : list) {
                    out << value;
                }
            } else if (prop.canConvert<QVector<qint64>>()) {
                auto list = prop.value<QVector<qint64>>();
                out.device()->write("l", 1);
                out << (int32_t)list.length();
                out << (int32_t)0;
                out << (int32_t)0;
                for (auto& value : list) {
                    out << value;
                }
            } else if (prop.canConvert<QVector<qint32>>()) {
                auto list = prop.value<QVector<qint32>>();
                out.device()->write("i", 1);
                out << (int32_t)list.length();
                out << (int32_t)0;
                out << (int32_t)0;
                for (auto& value : list) {
                    out << value;
                }
            } else if (prop.canConvert<QVector<bool>>()) {
                auto list = prop.value<QVector<bool>>();
                out.device()->write("b", 1);
                out << (int32_t)list.length();
                out << (int32_t)0;
                out << (int32_t)0;
                for (auto& value : list) {
                    out << value;
                }
            } else {
                qDebug() << "Unsupported property type in FBXWriter::encodeNode: " << type << prop;
            }
        }

    }
}
