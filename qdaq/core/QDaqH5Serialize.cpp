#include "QDaqObject.h"
#include "QDaqRoot.h"
#include "QDaqTypes.h"
#include <QMetaObject>
#include <QMetaProperty>
#include <QMetaEnum>
#include <QDebug>
#include <QDateTime>

#include <H5Cpp.h>

#include <string>

using namespace H5;

/* Rules for H5 serialization
 * - Objects are written as H5 groups
 * - Object properties are written as datasets of the group
 * - Base class QDaqObject::writeH5/readH5 handle all QDaq property types
 * - Other object data (like in QDaqDataBuffer) are also datasets
 * - need to override the basic writeH5/readH5 to handle the object special data reading/writing
 * - Root object -> write as any other obj / read as a plain QDaqObj
 */

#define CLASS_ATTR_NAME "Class"

void writeString(CommonFG* h5obj, const char* name, const QString& S)
{
    int len = S.length();
    if (len==0) return;
    StrType type(PredType::C_S1, len);
    hsize_t dims = 1;
    DataSpace space(1,&dims);
    DataSet ds = h5obj->createDataSet(name, type, space);
    ds.write(S.toLatin1().constData(),type);
}
bool readString(CommonFG* h5obj, const char* name, QString& str)
{
    DataSet ds = h5obj->openDataSet( name );
    H5T_class_t type_class = ds.getTypeClass();
    if (type_class==H5T_STRING)
    {
        StrType st = ds.getStrType();
        int sz = st.getSize();
        QByteArray buff(sz,'0');
        ds.read(buff.data(),st);
        str = QString(buff);
        return true;
    }
    return false;
}

void writeProperties(CommonFG* h5obj, const QDaqObject* m_object, const QMetaObject* metaObject);
void readProperties(CommonFG* h5obj, QDaqObject* obj);

void QDaqObject::writeH5(Group *h5g) const
{
    writeProperties(h5g,this,metaObject());
}

void writeRecursive(CommonFG* h5g, const QDaqObject* obj)
{
    H5::Group objGroup = h5g->createGroup(obj->objectName().toLatin1().constData());

    obj->writeH5(&objGroup);

    foreach(const QDaqObject* ch, obj->children()) writeRecursive(&objGroup, ch);

}

void QDaqObject::readH5(Group *h5g)
{
    readProperties(h5g,this);
}

void readRecursive(CommonFG* h5g, QDaqObject* &parent_obj)
{
    int n = h5g->getNumObjs();

    for(int i=0; i<n; ++i)
    {
        H5G_obj_t typ = h5g->getObjTypeByIdx(i);
        if (typ==H5G_GROUP)
        {
            QByteArray groupName;
            groupName.fill('\0',256);
            h5g->getObjnameByIdx(i,groupName.data(),256);
            Group g = h5g->openGroup(groupName);
            QString className;
            if (readString(&g,CLASS_ATTR_NAME,className))
            {
                if (className=="QDaqRoot") className = "QDaqObject";
                QDaqObject* obj  = QDaqObject::root()->createObject(groupName,className);
                if (obj)
                {
                    obj->setObjectName(groupName);
                    obj->readH5(&g);
                    readRecursive(&g, obj);
                    if (parent_obj) parent_obj->appendChild(obj);
                    else {
                        parent_obj = obj;
                        return;
                    }
                }
                else delete obj;
            }
        }
    }
}

void QDaqObject::h5write(const QDaqObject* obj, const QString& filename)
{
	QString S;
	H5File *file = 0;

    // Try block to detect exceptions raised by any of the calls inside it
    try
    {
        /*
         * Turn off the auto-printing when failure occurs so that we can
         * handle the errors appropriately
         */
        Exception::dontPrint();

        /*
         * Create the named file, truncating the existing one if any,
         * using default create and access property lists.
         */
		file = new H5File( filename.toLatin1(), H5F_ACC_TRUNC );

        writeString(file, "Timestamp", QDateTime::currentDateTime().toString(Qt::ISODate));
        writeString(file, "FileType", "QDaq");
        writeString(file, "FileVersionMajor", "1");
        writeString(file, "FileVersionMinor", "0");

        writeRecursive(file,obj);

    }  // end of try block

    // catch failure caused by the H5File operations
    catch( FileIException error )
    {
		S = "HDF5 File Error.";
		S += " In function ";
		S += error.getCFuncName();
		S += ". ";
		S += error.getCDetailMsg();
    }

    // catch failure caused by the DataSet operations
    catch( DataSetIException error )
    {
		S = "HDF5 DataSet Error.";
		S += " In function ";
		S += error.getCFuncName();
		S += ". ";
		S += error.getCDetailMsg();
    }

    // catch failure caused by the DataSpace operations
    catch( DataSpaceIException error )
    {
		S = "HDF5 DataSpace Error.";
		S += " In function ";
		S += error.getCFuncName();
		S += ". ";
		S += error.getCDetailMsg();
    }

    // catch failure caused by the Attribute operations
    catch( AttributeIException error )
    {
		S = "HDF5 Attribute Error.";
		S += " In function ";
		S += error.getCFuncName();
		S += ". ";
		S += error.getCDetailMsg();
    }

    // catch failure caused by the DataType operations
    catch( DataTypeIException error )
    {
		S = "HDF5 DataType Error.";
		S += " In function ";
		S += error.getCFuncName();
		S += ". ";
		S += error.getCDetailMsg();
    }

	if (file) delete file;

    if (!S.isEmpty())
    {
        obj->throwScriptError(S);
        qDebug() << S;
    }
}
QDaqObject *QDaqObject::h5read(const QString& filename)
{
    QString S;
    H5File *file = 0;
    QDaqObject* obj(0);

    // Try block to detect exceptions raised by any of the calls inside it
    try
    {
        /*
         * Turn off the auto-printing when failure occurs so that we can
         * handle the errors appropriately
         */
        Exception::dontPrint();

        /*
         * Open the named file with read-only access
         */
        file = new H5File( filename.toLatin1(), H5F_ACC_RDONLY );

        QString fileType, vMajor, vMinor;

        bool ret = readString(file,"FileType",fileType) &&
                readString(file,"FileVersionMajor",vMajor) &&
                readString(file,"FileVersionMinor",vMinor) &&
                fileType==QString("QDaq")
                && vMajor==QString("1")
                && vMinor==QString("0");

        if (ret) readRecursive(file,obj);


    }  // end of try block

    // catch failure caused by the H5File operations
    catch( FileIException error )
    {
        S = "HDF5 File Error.";
        S += " In function ";
        S += error.getCFuncName();
        S += ". ";
        S += error.getCDetailMsg();
    }

    // catch failure caused by the DataSet operations
    catch( DataSetIException error )
    {
        S = "HDF5 DataSet Error.";
        S += " In function ";
        S += error.getCFuncName();
        S += ". ";
        S += error.getCDetailMsg();
    }

    // catch failure caused by the DataSpace operations
    catch( DataSpaceIException error )
    {
        S = "HDF5 DataSpace Error.";
        S += " In function ";
        S += error.getCFuncName();
        S += ". ";
        S += error.getCDetailMsg();
    }

    // catch failure caused by the Attribute operations
    catch( AttributeIException error )
    {
        S = "HDF5 Attribute Error.";
        S += " In function ";
        S += error.getCFuncName();
        S += ". ";
        S += error.getCDetailMsg();
    }

    // catch failure caused by the DataType operations
    catch( DataTypeIException error )
    {
        S = "HDF5 DataType Error.";
        S += " In function ";
        S += error.getCFuncName();
        S += ". ";
        S += error.getCDetailMsg();
    }

    if (file) delete file;

    if (!S.isEmpty())
    {
        //obj->throwScriptError(S);
        qDebug() << S;
    }

    return obj;
}

template<class _VectorType>
void writeVectorClass(CommonFG* h5obj, const char* name, const QVariant& value, const DataType& type)
{
    _VectorType v = value.value<_VectorType>();
    hsize_t sz = v.size();
    if (sz<1) sz=1;
    hsize_t dims = 1;
    DataSpace space(1,&dims);
    DataSet ds = h5obj->createDataSet(name, type, space);
    if (v.size()) ds.write(v.constData(),type);
}

void writeProperties(CommonFG* h5obj, const QDaqObject* m_object, const QMetaObject* metaObject)
{
	// get the super-class meta-object
	const QMetaObject* super = metaObject->superClass();

	// if it is null -> we reached the top, i.e. the QObject level -> return
	if (!super) return;

	// if this is the first , write the class name
	if (metaObject==m_object->metaObject())
	{
        writeString(h5obj,CLASS_ATTR_NAME, metaObject->className());
	}

	// first write the properties of the super-class (this produces all properties up to the current object)
	writeProperties(h5obj, m_object, super);

	// write this class properties
	for (int idx = metaObject->propertyOffset(); idx < metaObject->propertyCount(); idx++) 
	{
		QMetaProperty metaProperty = metaObject->property(idx);
		if (metaProperty.isReadable())
		{
			QVariant value = metaProperty.read(m_object);
			if (metaProperty.isEnumType())
			{
				QMetaEnum metaEnum = metaProperty.enumerator();
				int i = *reinterpret_cast<const int *>(value.constData());
                writeString(h5obj,metaProperty.name(),metaEnum.valueToKey(i));
			}
			else {
				int objtype = value.userType();
				if (objtype==QVariant::Bool || objtype==QVariant::Char ||
					objtype==QVariant::Int || objtype==QVariant::UInt)
				{
					DataSpace space(H5S_SCALAR);
                    DataSet ds = h5obj->createDataSet(metaProperty.name(),
                        PredType::NATIVE_INT, space);
					int i = value.toInt();
                    ds.write(&i,PredType::NATIVE_INT);
				}
				else if (objtype==QVariant::String)
				{
					QString S = value.toString();
					if (S.isEmpty()) S=" ";
                    writeString(h5obj,metaProperty.name(),S);
				}
				else if (objtype==QVariant::Double)
				{
					DataSpace space(H5S_SCALAR);
                    DataSet ds = h5obj->createDataSet(metaProperty.name(),
                        PredType::NATIVE_DOUBLE, space);
					double d = value.toDouble();
                    ds.write(&d,PredType::NATIVE_DOUBLE);
				}
                else if (objtype==qMetaTypeId<QDaqIntVector>())
                    writeVectorClass<QDaqIntVector>(h5obj,metaProperty.name(),value,PredType::NATIVE_INT);
                else if (objtype==qMetaTypeId<QDaqUintVector>())
                    writeVectorClass<QDaqUintVector>(h5obj,metaProperty.name(),value,PredType::NATIVE_UINT);
                else if (objtype==qMetaTypeId<QDaqDoubleVector>())
                    writeVectorClass<QDaqDoubleVector>(h5obj,metaProperty.name(),value,PredType::NATIVE_DOUBLE);
			}
		}
	}
}

template<class _T>
bool readScalar(CommonFG* h5obj, const char* name, _T& value, const H5T_class_t& type)
{
    DataSet ds = h5obj->openDataSet(name);
    H5T_class_t ds_type = ds.getTypeClass();
    if (ds_type==type)
    {
        ds.read(&value, ds.getDataType());
        return true;
    }
    return false;
}

template<class _T>
bool readVectorClass(CommonFG* h5obj, const char* name, _T& value, const H5T_class_t& type)
{
    DataSet ds = h5obj->openDataSet(name);
    H5T_class_t ds_type = ds.getTypeClass();
    if (ds_type==type)
    {
        DataSpace dspace = ds.getSpace();
        int sz = dspace.getSimpleExtentNpoints();
        value.fill(0,sz);
        ds.read(value.data(), ds.getDataType());
        return true;
    }
    return false;
}

void readProperties(CommonFG *h5obj, QDaqObject* obj)
{
    // get the meta-object
    const QMetaObject* metaObject = obj->metaObject();

    // the "class" and "objectName" properties have been read when the obj was constructed
    // start at index = 2
    for (int idx = 2; idx < metaObject->propertyCount(); idx++)
    {
        QMetaProperty metaProperty = metaObject->property(idx);
        if (metaProperty.isWritable())
        {
            if (metaProperty.isEnumType())
            {
                QMetaEnum metaEnum = metaProperty.enumerator();
                QString key;
                if (readString(h5obj,metaProperty.name(),key))
                {
                    int v = metaEnum.keyToValue(key.toLatin1().constData());
                    if (v>=0)
                        metaProperty.write(obj,QVariant(v));
                }
            }
            else {
                int objtype = metaProperty.type();
                if (objtype==QVariant::Bool || objtype==QVariant::Char ||
                    objtype==QVariant::Int || objtype==QVariant::UInt)
                {
                    int v;
                    if (readScalar(h5obj,metaProperty.name(),v,H5T_INTEGER))
                        metaProperty.write(obj,QVariant(objtype,&v));
                }
                else if (objtype==QVariant::String)
                {
                    QString v;
                    if (readString(h5obj,metaProperty.name(),v))
                        metaProperty.write(obj,QVariant(v));
                }
                else if (objtype==QVariant::Double)
                {
                    double v;
                    if (readScalar(h5obj,metaProperty.name(),v,H5T_FLOAT))
                        metaProperty.write(obj,QVariant(v));
                }
                else if (objtype==qMetaTypeId<QDaqIntVector>())
                {
                    QDaqIntVector v;
                    if (readVectorClass(h5obj,metaProperty.name(),v,H5T_INTEGER))
                        metaProperty.write(obj,QVariant::fromValue(v));
                }
                else if (objtype==qMetaTypeId<QDaqUintVector>())
                {
                    QDaqUintVector v;
                    if (readVectorClass(h5obj,metaProperty.name(),v,H5T_INTEGER))
                        metaProperty.write(obj,QVariant::fromValue(v));
                }
                else if (objtype==qMetaTypeId<QDaqDoubleVector>())
                {
                    QDaqDoubleVector v;
                    if (readVectorClass(h5obj,metaProperty.name(),v,H5T_FLOAT))
                        metaProperty.write(obj,QVariant::fromValue(v));
                }
            }
        }
    }
}


