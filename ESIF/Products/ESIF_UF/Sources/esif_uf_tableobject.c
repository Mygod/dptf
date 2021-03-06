/******************************************************************************
** Copyright (c) 2013-2017 Intel Corporation All Rights Reserved
**
** Licensed under the Apache License, Version 2.0 (the "License"); you may not
** use this file except in compliance with the License.
**
** You may obtain a copy of the License at
**     http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
** WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**
** See the License for the specific language governing permissions and
** limitations under the License.
**
******************************************************************************/
#define ESIF_TRACE_ID	ESIF_TRACEMODULE_TABLEOBJECT

#include "esif_uf_version.h"
#include "esif_uf.h"		/* Upper Framework */
#include "esif_pm.h"		/* Upper Participant Manager */
#include "esif_uf_cfgmgr.h"	/* Config Manager */
#include "esif_uf_eventmgr.h" /* Event Manager */
#include "esif_uf_primitive.h" /* Execute Primitive */

#define _DATABANK_CLASS
#include "esif_lib_databank.h"
#include "esif_dsp.h"
#include "esif_uf_tableobject.h"
#include "esif_temp.h"

#ifdef ESIF_ATTR_OS_WINDOWS
//
// The Windows banned-API check header must be included after all other headers, or issues can be identified
// against Windows SDK/DDK included headers which we have no control over.
//
#define _SDL_BANNED_RECOMMENDED
#include "win\banned.h"
#endif

#define MAX_TABLEOBJECT_KEY_LEN 256
#define MAX_TABLEOBJECT_BINARY  0x7fffffff
#define COLUMN_MAX_SIZE 64
#define REVISION_INDICATOR_LENGTH 2
#define MODE_INDICATOR_LENGTH 2
#define ACPI_NAME_TARGET_SIZE 4
#define MARKED 1
#define UNMARKED 0
#define ESIF_DATA_PREFIX_SIZE 10
#define ESIF_NO_PERSIST_INSTANCE 254
#define ESIF_NO_INSTANCE 255
#define ESIF_NO_PERSIST_OFFSET 200

#pragma pack(push, 1)
struct esif_data_binary_bst_package {
	union esif_data_variant battery_state;
	union esif_data_variant battery_present_rate;
	union esif_data_variant battery_remaining_capacity;
	union esif_data_variant battery_present_voltage;
};

struct esif_data_binary_ppcc_package {
	union esif_data_variant pl_index;
	union esif_data_variant pl_min;
	union esif_data_variant pl_max;
	union esif_data_variant time_window_min;
	union esif_data_variant time_window_max;
	union esif_data_variant step_size;
};

struct guid_t {
	u32 data1;
	u16 data2;
	u16 data3;
	u8 data4[8];
};
#pragma pack(pop)

typedef struct TableDataPiece_s {
	int newRow;
	char dataPiece[200];
	char fieldTag[50];
	EsifDataType dataType;
	struct TableDataPiece_s *prevDataPiece;
	struct TableDataPiece_s *nextDataPiece;
	int isRevision;
	int isMode;
} TableDataPiece;

void TableField_Construct(
	TableField *self,
	char *name,
	char *label,
	EsifDataType dataType,
	int notForXML
	)
{
	esif_ccb_memset(self, 0, sizeof(*self));
	self->name = name;
	self->label = label;
	self->dataType = dataType;
	self->notForXML = notForXML;
}

void TableField_ConstructAs(
	TableField *self,
	char *name,
	char *label,
	EsifDataType dataType,
	int notForXML,
	int getPrimitive,
	int setPrimitive,
	UInt8 instance,
	char *dataVaultKey
	)
{
	esif_ccb_memset(self, 0, sizeof(*self));
	self->name = name;
	self->label = label;
	self->dataType = dataType;
	self->notForXML = notForXML;
	self->getPrimitive = getPrimitive;
	self->setPrimitive = setPrimitive;
	self->instance = instance;
	self->dataVaultKey = dataVaultKey;
}

void TableField_Destroy(TableField *self)
{
	esif_ccb_memset(self, 0, sizeof(*self));
}

eEsifError TableObject_ResetConfig(
	TableObject *self,
	UInt32 primitiveToReset,
	const UInt8 instance
	)
{
	eEsifError rc = ESIF_OK;
	char domainStr[8] = "";

	EsifPrimitiveTupleParameter parameters = { 0 };

	if (primitiveToReset != 0)
	{
		parameters.id.integer.value = primitiveToReset;
		parameters.domain.integer.value = domain_str_to_short(self->domainQualifier);
		parameters.instance.integer.value = instance;

		struct esif_data request = { ESIF_DATA_BINARY, &parameters, sizeof(parameters), sizeof(parameters) };
		struct esif_data response = { ESIF_DATA_VOID, NULL, 0, 0 };

		rc = EsifExecutePrimitive((UInt8)self->participantId, SET_CONFIG_RESET,
			esif_primitive_domain_str((u16)ESIF_PRIMITIVE_DOMAIN_D0, domainStr, sizeof(domainStr)), ESIF_NO_INSTANCE, &request, &response);
	}

	return rc;
}

eEsifError TableObject_Save(TableObject *self)
{
	int i = 0;
	int numFields = 0;
	char *textInput;
	EsifDataType objDataType;
	char *tableRow = NULL;
	char *rowTok = NULL;
	char *tableCol = NULL;
	char *tmp;
	char *colTok = NULL;
	char rowDelims [] = "!";
	char colDelims [] = ",";
	eEsifError rc = ESIF_OK;
	EsifDataPtr data_nspace = NULL;
	EsifDataPtr data_path = NULL;
	EsifDataPtr data_value = NULL;
	EsifDataPtr data_key = NULL;
	esif_flags_t options = ESIF_SERVICE_CONFIG_PERSIST | ESIF_SERVICE_CONFIG_NOCACHE;

	textInput = self->dataText;
	numFields = self->numFields;
	objDataType = self->dataType;

	if (objDataType == ESIF_DATA_BINARY) {
		rc = TableObject_Convert(self);
		
		if (rc != ESIF_OK) {
			goto exit;
		}
		
		if (self->binaryDataSize > MAX_TABLEOBJECT_BINARY) {
			rc = ESIF_E_PARAMETER_IS_OUT_OF_BOUNDS;
		}
		else if (self->binaryDataSize) {
			struct esif_data request = { ESIF_DATA_BINARY, self->binaryData, self->binaryDataSize, self->binaryDataSize };
			struct esif_data response = { ESIF_DATA_VOID, NULL, 0, 0 };

			if (self->dataSource) {  //our alternate datasource is the datavault
				u8 *targetData = esif_ccb_malloc(self->binaryDataSize);

				data_nspace = EsifData_CreateAs(ESIF_DATA_STRING, self->dataSource, 0, ESIFAUTOLEN);
				data_path = EsifData_CreateAs(ESIF_DATA_STRING, self->dataMember, 0, ESIFAUTOLEN);
				data_value = EsifData_CreateAs(ESIF_DATA_BINARY, targetData, self->binaryDataSize, self->binaryDataSize);

				if (data_nspace && data_path && data_value && targetData) {
					esif_ccb_memcpy((u8 *)targetData, (u8 *)self->binaryData, self->binaryDataSize);
					rc = EsifConfigSet(data_nspace, data_path, options, data_value);
				}
				else {
					esif_ccb_free(targetData);
					rc = ESIF_E_NO_MEMORY;
				}

			}
			else {
				TableObject_ResetConfig(self, self->setPrimitive, ESIF_NO_PERSIST_INSTANCE);
				rc = EsifExecutePrimitive((UInt8)self->participantId, self->setPrimitive, self->domainQualifier, ESIF_NO_INSTANCE, &request, &response);
			}
		}
		else {
			rc = ESIF_E_UNSPECIFIED;
		}
	}
	/* These are virtual tables, which are collections of potentially unrelated primitives, grouped
	together to form a data set */
	else {
		UInt32 numberHolder32 = 0;
		UInt64 numberHolder64 = 0;
		int rowCounter = -1;
		char stringHolder[COLUMN_MAX_SIZE] = { 0 };
		struct esif_data request = { ESIF_DATA_VOID };
		struct esif_data response = { ESIF_DATA_VOID, NULL, 0, 0 };

		/* Wipe any table-level datavault keys so that new key table can be regenerated*/
		if (self->dataMember != NULL) {
			if (self->dataSource == NULL) {
				self->dataSource = esif_ccb_strdup(ESIF_DSP_OVERRIDE_NAMESPACE); //default to override
			}
			
			data_nspace = EsifData_CreateAs(ESIF_DATA_STRING, self->dataSource, 0, ESIFAUTOLEN);
			data_key = EsifData_CreateAs(ESIF_DATA_STRING, self->dataMember, 0, ESIFAUTOLEN);

			if (data_nspace == NULL || data_key == NULL) {
				rc = ESIF_E_NO_MEMORY;
				goto exit;
			}

			TableObject_ResetConfig(self, self->setPrimitive, ESIF_NO_PERSIST_INSTANCE);
			rc = EsifConfigDelete(data_nspace, data_key);

		}

		tableRow = esif_ccb_strtok(textInput, rowDelims, &rowTok);
		while (tableRow != NULL) {
			i = -1;
			rowCounter++;
			tableCol = esif_ccb_strtok(tableRow, colDelims, &colTok);
			while (tableCol != NULL) {
				u64 colValueLen = esif_ccb_strlen(tableCol, COLUMN_MAX_SIZE) + 1;
				UNREFERENCED_PARAMETER(colValueLen);
				i++;
				if (i < numFields) {
					if ((tmp = esif_ccb_strstr(tableCol, "'")) != NULL) {
						*tmp = 0;
					}
					if (self->fields[i].setPrimitive > 0) {
						request.type = self->fields[i].dataType;
						switch (self->fields[i].dataType) {
						case ESIF_DATA_STRING:
							//tableCol holds value
							break;
						case ESIF_DATA_UINT32:
						case ESIF_DATA_POWER:
						case ESIF_DATA_TEMPERATURE:
						case ESIF_DATA_TIME:
							numberHolder32 = (UInt32)esif_atoi(tableCol);
							request.buf_len = sizeof(numberHolder32);
							request.data_len = sizeof(numberHolder32);
							request.buf_ptr = &numberHolder32;
							if (self->fields[i].instance != ESIF_NO_INSTANCE)
							{
								TableObject_ResetConfig(self, self->fields[i].setPrimitive, self->fields[i].instance + ESIF_NO_PERSIST_OFFSET);
							}
							else
							{
								TableObject_ResetConfig(self, self->fields[i].setPrimitive, ESIF_NO_PERSIST_INSTANCE);
							}
							rc = EsifExecutePrimitive((UInt8)self->participantId, self->fields[i].setPrimitive, self->domainQualifier, self->fields[i].instance, &request, &response);
							break;
						case ESIF_DATA_UINT64:
							numberHolder64 = (UInt64) esif_atoi(tableCol);
							request.buf_len = sizeof(numberHolder64);
							request.data_len = sizeof(numberHolder64);
							request.buf_ptr = &numberHolder64;
							rc = EsifExecutePrimitive((UInt8) self->participantId, self->fields[i].setPrimitive, self->domainQualifier, self->fields[i].instance, &request, &response);
							break;
						default:
							ESIF_TRACE_DEBUG("Field type in schema for table %s is not handled \n", self->name);
							rc = ESIF_E_PARAMETER_IS_OUT_OF_BOUNDS;
							goto exit;
							break;
						}
					}
					else {  //config keys only - no corresponding primitive
						char enumeratedKey[MAX_TABLEOBJECT_KEY_LEN] = { 0 };

						options = ESIF_SERVICE_CONFIG_PERSIST | ESIF_SERVICE_CONFIG_NOCACHE;

						if (self->fields[i].dataVaultKey == NULL) {
							rc = ESIF_E_NOT_IMPLEMENTED;
							goto exit;
						}
						EsifData_Destroy(data_value);
						data_value = EsifData_Create();		

						esif_ccb_sprintf(MAX_TABLEOBJECT_KEY_LEN, enumeratedKey, "%s/%03d", self->fields[i].dataVaultKey, rowCounter + 1);
						esif_ccb_sprintf(COLUMN_MAX_SIZE, stringHolder, "%s", tableCol);

						rc = EsifData_FromString(data_value, stringHolder, ESIF_DATA_STRING);

						if (rc != ESIF_OK) {
							goto exit;
						}

						EsifData_Destroy(data_nspace);
						EsifData_Destroy(data_key);
						data_nspace = EsifData_CreateAs(ESIF_DATA_STRING, self->dataSource, 0, ESIFAUTOLEN);
						data_key = EsifData_CreateAs(ESIF_DATA_STRING, enumeratedKey, 0, ESIFAUTOLEN);
						
						if (data_nspace == NULL || data_key == NULL || data_value == NULL) {
							rc = ESIF_E_NO_MEMORY;
							goto exit;
						}

						rc = EsifConfigSet(data_nspace, data_key, options, data_value);

					}
				}
				// Get next column
				tableCol = esif_ccb_strtok(NULL, colDelims, &colTok);
			}
			// Get next row
			tableRow = esif_ccb_strtok(NULL, rowDelims, &rowTok);
		}
	}

exit:
	EsifData_Destroy(data_nspace);
	EsifData_Destroy(data_path);
	EsifData_Destroy(data_value);
	EsifData_Destroy(data_key);

	return rc;
}

eEsifError TableObject_LoadData(
	TableObject *self
	)
{
	eEsifError rc = ESIF_OK;
	u8 *tmp_buf = NULL;
	struct esif_data request = { ESIF_DATA_VOID, NULL, 0, 0 };
	union esif_data_variant *obj;
	char *textInput = NULL;
	char revisionNumString[REVISION_INDICATOR_LENGTH + 1];
	char modeString[MODE_INDICATOR_LENGTH + 1];
	u64 revisionNumInt = 0;
	u64 modeNumInt = 0;
	EsifDataPtr namespacePtr = NULL;
	EsifDataPtr pathPtr = NULL;
	EsifDataPtr responsePtr = NULL;

	textInput = self->dataText;

	//if the table is not binary, the version is automatically 1
	if (self->dataType != ESIF_DATA_BINARY){
		self->version = 1;
		rc = ESIF_OK;
		goto exit;
	}
	
	if (self->mode == SET) { //for SET, the only action here is to extract the version
		if (FLAGS_TEST(self->options, TABLEOPT_CONTAINS_REVISION) && esif_ccb_strlen(textInput, REVISION_INDICATOR_LENGTH + 1) > REVISION_INDICATOR_LENGTH) {
			esif_ccb_memcpy(&revisionNumString, textInput, REVISION_INDICATOR_LENGTH);
			revisionNumString[REVISION_INDICATOR_LENGTH] = '\0';
			if (FLAGS_TEST(self->options, TABLEOPT_CONTAINS_MODE)) {
				textInput += MODE_INDICATOR_LENGTH + 1;
				esif_ccb_memcpy(&modeString, textInput, MODE_INDICATOR_LENGTH);
				modeString[MODE_INDICATOR_LENGTH] = '\0';
				modeNumInt = esif_atoi(modeString);
				self->controlMode = (u64) modeNumInt;
			}
			revisionNumInt = esif_atoi(revisionNumString);
			self->version = (u64) revisionNumInt;
		}

	}
	else { //for GET, extract version and execute based on datasource/member
		responsePtr = EsifData_CreateAs(ESIF_DATA_AUTO, NULL, ESIF_DATA_ALLOCATE, 0);
		if (NULL == responsePtr) {
			rc = ESIF_E_NO_MEMORY;
			goto exit;
		}

		if (self->dataSource != NULL && self->dataMember != NULL) { //currenly our only alternative datasource is datavault
			namespacePtr = EsifData_CreateAs(ESIF_DATA_STRING, self->dataSource, 0, ESIFAUTOLEN);
			pathPtr = EsifData_CreateAs(ESIF_DATA_STRING, self->dataMember, 0, ESIFAUTOLEN);
			if (namespacePtr == NULL || pathPtr == NULL) {
				rc = ESIF_E_NO_MEMORY;
				goto exit;
			}
			rc = EsifConfigGet(namespacePtr, pathPtr, responsePtr);
			if (ESIF_OK != rc) {
				goto exit;
			}
			self->binaryData = esif_ccb_malloc(responsePtr->buf_len);
			esif_ccb_memcpy((u8 *) self->binaryData, responsePtr->buf_ptr, responsePtr->buf_len);
			self->binaryDataSize = responsePtr->buf_len;
		}
		else {
			tmp_buf = (u8 *) esif_ccb_malloc(OUT_BUF_LEN);
			if ((NULL == tmp_buf)) {
				rc = ESIF_E_NO_MEMORY;
				goto exit;
			}
			responsePtr->type = self->dataType;
			responsePtr->buf_ptr = tmp_buf;
			responsePtr->buf_len = OUT_BUF_LEN;
			if (!self->binaryData){
				rc = EsifExecutePrimitive((UInt8)self->participantId, self->getPrimitive, self->domainQualifier, ESIF_NO_INSTANCE, &request, responsePtr);
				if (ESIF_OK != rc) {
					goto exit;
				}
				self->binaryData = esif_ccb_malloc(responsePtr->data_len);
				esif_ccb_memcpy((u8 *) self->binaryData, (u8 *) responsePtr->buf_ptr, responsePtr->data_len);
				self->binaryDataSize = responsePtr->data_len;
			}
		}
		
		if (self->binaryData != NULL) {
			obj = (union esif_data_variant *)self->binaryData;

			if (FLAGS_TEST(self->options, TABLEOPT_CONTAINS_REVISION)) {
				self->version = (u64) obj->integer.value;
			}

			if (FLAGS_TEST(self->options, TABLEOPT_CONTAINS_MODE)) {
				obj = (union esif_data_variant *)((u8 *) obj + sizeof(*obj));
				self->controlMode = (u64) obj->integer.value;
			}
		}
	}
	
exit:
	EsifData_Destroy(namespacePtr);
	EsifData_Destroy(pathPtr);
	EsifData_Destroy(responsePtr);
	return rc;
}

eEsifError TableObject_LoadXML(
	TableObject *self,
	enum esif_temperature_type tempXformType 
	)
{
	int i = 1;
	int totalRows = 0;
	Bool dataFound = ESIF_FALSE;
	EsifDataType targetType;
	char *output = esif_ccb_malloc(OUT_BUF_LEN);
	union esif_data_variant *obj;
	int remain_bytes = 0;
	char *strFieldValue = NULL;
	u32 int32FieldValue = 0;
	u64 int64FieldValue = 0;
	eEsifError rc = ESIF_OK;
	eEsifError primitiveOK = ESIF_OK;  /* for use with virtual tables, nonfatal errors */
	u8 *tmp_buf = (u8 *) esif_ccb_malloc(OUT_BUF_LEN);
	UInt32 defaultNumber = 0;
	EsifDataType objDataType = self->dataType;
	struct esif_data request = { ESIF_DATA_VOID, NULL, 0, 0 };
	struct esif_data response = { ESIF_DATA_VOID };
	EsifDataPtr  data_nspace = NULL;
	EsifDataPtr  data_key = NULL;
	Bool cursorState = UNMARKED;
	struct esif_link_list *TableDataListPtr = NULL;
	struct esif_link_list_node *curr_ptr = NULL;
	
	TableDataListPtr = esif_link_list_create();

	if ((NULL == tmp_buf) || (NULL == output) || (NULL == TableDataListPtr)) {
		rc = ESIF_E_NO_MEMORY;
		goto exit;
	}
	*output = '\0';

	response.data_len = 0;

	if (objDataType == ESIF_DATA_BINARY) {
		response.type = objDataType;
		response.buf_ptr = tmp_buf;
		response.buf_len = OUT_BUF_LEN;
		if (!self->binaryData){  //Now requires pre-population from LoadData
			rc = ESIF_E_PARAMETER_IS_NULL;
			goto exit;
		}
		else {
			remain_bytes = self->binaryDataSize;
		}
		obj = (union esif_data_variant *)self->binaryData;
		

		/* if the table has a revision, load that in and shift the bytes
		before looping through the fields */
		if (FLAGS_TEST(self->options, TABLEOPT_CONTAINS_REVISION)) {
			TableDataPiece *revisionData = NULL;
			struct esif_link_list_node *nodePtr = NULL;
			revisionData = (TableDataPiece *)esif_ccb_malloc(sizeof(TableDataPiece));
			if (revisionData == NULL) {
				rc = ESIF_E_NO_MEMORY;
				goto exit;
			}
			revisionData->nextDataPiece = 0;
			revisionData->newRow = 0;
			revisionData->isRevision = 1;
			int64FieldValue = (u64)obj->integer.value;
			esif_ccb_sprintf(sizeof(revisionData->dataPiece), revisionData->dataPiece, "%lld", int64FieldValue);
			esif_ccb_strcpy(revisionData->fieldTag, "revision", sizeof(revisionData->fieldTag));
			revisionData->dataType = ESIF_DATA_UINT64;
			nodePtr = esif_link_list_create_node(revisionData);
			if (NULL == nodePtr) {
				rc = ESIF_E_NO_MEMORY;
				goto exit;
			}
			esif_link_list_add_node_at_back(TableDataListPtr, nodePtr);
			if (FLAGS_TEST(self->options, TABLEOPT_CONTAINS_MODE)) {
				obj = (union esif_data_variant *)((u8 *)obj + sizeof(*obj));
				TableDataPiece *modeData = NULL;
				modeData = (TableDataPiece *)esif_ccb_malloc(sizeof(TableDataPiece));
				if (modeData == NULL) {
					rc = ESIF_E_NO_MEMORY;
					goto exit;
				}
				modeData->nextDataPiece = 0;
				modeData->newRow = 0;
				modeData->isMode = 1;
				int64FieldValue = (u64)obj->integer.value;
				esif_ccb_sprintf(sizeof(modeData->dataPiece), modeData->dataPiece, "%lld", int64FieldValue);
				esif_ccb_strcpy(modeData->fieldTag, "mode", sizeof(modeData->fieldTag));
				modeData->dataType = ESIF_DATA_UINT64;
				nodePtr = esif_link_list_create_node(modeData);
				if (NULL == nodePtr) {
					rc = ESIF_E_NO_MEMORY;
					goto exit;
				}
				esif_link_list_add_node_at_back(TableDataListPtr, nodePtr);
			}
			obj = (union esif_data_variant *)((u8 *)obj + sizeof(*obj));
			if (FLAGS_TEST(self->options, TABLEOPT_CONTAINS_MODE)) {
				remain_bytes -= (sizeof(*obj) + sizeof(*obj));
			}
			else
			{
				remain_bytes -= sizeof(*obj);
			}
		}

		/* loop through the fields that were provided by _LoadSchema */
		while (remain_bytes >= sizeof(*obj)) {
			esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "  <trtRow>\n");
			for (i = 0; (remain_bytes >= sizeof(*obj) && (i < self->numFields || self->dynamicColumnCount == 1)); i++) {
				TableDataPiece *newData = NULL;
				struct esif_link_list_node *nodePtr = NULL;
				newData = (TableDataPiece *) esif_ccb_malloc(sizeof(TableDataPiece));
				if (newData == NULL) {
					rc = ESIF_E_NO_MEMORY;
					goto exit;
				}
				newData->nextDataPiece = 0;
				newData->newRow = 0;
				newData->isRevision = 0;
				nodePtr = esif_link_list_create_node(newData);
				if (NULL == nodePtr) {
					rc = ESIF_E_NO_MEMORY;
					goto exit;
				}
				esif_link_list_add_node_at_back(TableDataListPtr, nodePtr);
				remain_bytes -= sizeof(*obj);
				ESIF_TRACE_DEBUG("Obtaining bios binary data for table: %s, field: %s, type: %d \n", self->name, self->fields[i].name, self->fields[i].dataType);
				targetType = (FLAGS_TEST(self->options, TABLEOPT_ALLOW_SELF_DEFINE) ? obj->type : self->fields[i].dataType);
				switch (targetType) {
							
				case ESIF_DATA_STRING:
					if (obj->type != ESIF_DATA_STRING) {
						ESIF_TRACE_DEBUG("While loading field: %s into table: %s, field datatype mismatch detected (expecting STRING). \n", self->fields[i].name, self->name);
						rc = ESIF_E_PARAMETER_IS_OUT_OF_BOUNDS;
						goto exit;
					}
					strFieldValue = (char *) ((u8 *) obj + sizeof(*obj));
					remain_bytes -= obj->string.length;
					obj = (union esif_data_variant *)((u8 *) obj + (sizeof(*obj) + obj->string.length));
					ESIF_TRACE_DEBUG("Determined field value: %s \n", strFieldValue);
					esif_ccb_sprintf(sizeof(newData->dataPiece), newData->dataPiece, "%s", strFieldValue);
					break;
				case ESIF_DATA_UINT32:
					if (obj->type != ESIF_DATA_UINT32) {
						ESIF_TRACE_DEBUG("While loading field: %s into table: %s, field datatype mismatch detected (expecting 32 bit integer). \n", self->fields[i].name, self->name);
						rc = ESIF_E_PARAMETER_IS_OUT_OF_BOUNDS;
						goto exit;
					}
					int32FieldValue = (u32) obj->integer.value;
					obj = (union esif_data_variant *)((u8 *) obj + sizeof(*obj));
					ESIF_TRACE_DEBUG("Determined field value: %d \n", int32FieldValue);
					esif_ccb_sprintf(sizeof(newData->dataPiece), newData->dataPiece, "%d", int32FieldValue);
					break;
				case ESIF_DATA_UINT64:
					//UInt32's are allowed - values will be casted up
					if (obj->type != ESIF_DATA_UINT32 && obj->type != ESIF_DATA_UINT64) {
						ESIF_TRACE_DEBUG("While loading field: %s into table: %s, field datatype mismatch detected (expecting 64 bit integer, 32 bit okay). \n", self->fields[i].name, self->name);
						rc = ESIF_E_PARAMETER_IS_OUT_OF_BOUNDS;
						goto exit;
					}
					int64FieldValue = (u64) obj->integer.value;
					obj = (union esif_data_variant *)((u8 *) obj + sizeof(*obj));
					ESIF_TRACE_DEBUG("Determined field value: %lld \n", int64FieldValue);
					esif_ccb_sprintf(sizeof(newData->dataPiece), newData->dataPiece, "%lld", int64FieldValue);
					break;
				default:
					ESIF_TRACE_DEBUG("Field type in schema for table %s is not handled \n", self->name);
					rc = ESIF_E_PARAMETER_IS_OUT_OF_BOUNDS;
					goto exit;
					break;
				}
				if (self->dynamicColumnCount) {
					esif_ccb_sprintf(sizeof(newData->fieldTag), newData->fieldTag, "%s_Field", esif_data_type_str(targetType) + ESIF_DATA_PREFIX_SIZE);
				}
				else {
					esif_ccb_sprintf(sizeof(newData->fieldTag), newData->fieldTag, "%s", self->fields[i].name);
				}
				newData->dataType = targetType;
			}
		}
		curr_ptr = TableDataListPtr->head_ptr;
		esif_ccb_strcpy(output, "<result>\n", OUT_BUF_LEN);
		if (FLAGS_TEST(self->options, TABLEOPT_CONTAINS_REVISION)) {
			esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "<revision>\n");
		}
		else if (curr_ptr != NULL) {
			totalRows++;
			esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "<row>\n");
		}
		i = 0;
		while (curr_ptr != NULL) {
			TableDataPiece *tdp = (TableDataPiece *)curr_ptr->data_ptr;
			if (NULL == tdp) /* Should never happen */
				continue;
			if (self->dynamicColumnCount) {
				if (tdp->dataType == ESIF_DATA_STRING && cursorState == MARKED) {
					TableDataPiece *pdp = (TableDataPiece *) curr_ptr->prev_ptr->data_ptr;
					pdp->newRow = 1;
					cursorState = UNMARKED;
				}
				else if (curr_ptr->prev_ptr != NULL) {
					TableDataPiece *pdp = (TableDataPiece *) curr_ptr->prev_ptr->data_ptr;
					if (pdp->dataType == ESIF_DATA_UINT64 && cursorState == UNMARKED && i > 0) {
						cursorState = MARKED;
					}
				}
			}
			else {
				if (i % self->numFields == 0 && i > 0) {
					tdp->newRow = 1;
				}
			}
			
			if (!tdp->isRevision) {
				if (FLAGS_TEST(self->options, TABLEOPT_CONTAINS_MODE)) {
					if (!tdp->isMode) {
						i++;
						dataFound = ESIF_TRUE;
					}
				}
				else
				{
					i++;
					dataFound = ESIF_TRUE;
				}

			}
			
			curr_ptr = curr_ptr->next_ptr;
			
		}
		curr_ptr = TableDataListPtr->head_ptr;
		while (curr_ptr != NULL) {
			TableDataPiece *tdp = (TableDataPiece *) curr_ptr->data_ptr;
			if (NULL == tdp) /* Should never happen */
				continue;
			if (esif_ccb_strcmp(tdp->dataPiece, "")) {
				if (tdp->newRow) {
					totalRows++;
					esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "</row>\n<row>\n");
				}
				if (tdp->isRevision) {
					esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "    %s\n", tdp->dataPiece);
					if (!FLAGS_TEST(self->options, TABLEOPT_CONTAINS_MODE) && dataFound) {
						esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "</revision>\n<row>\n");
					}
					else if (!FLAGS_TEST(self->options, TABLEOPT_CONTAINS_MODE))
					{
						esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "</revision>\n");
					}
					else {
						esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "</revision>\n<mode>\n");
					}
				}
				else if (tdp->isMode) {
					esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "    %s\n", tdp->dataPiece);
					if (dataFound)
					{
						esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "</mode>\n<row>\n");
					}
					else
					{
						esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "</mode>\n");
					}
				}
				else {
					esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "    <%s>%s</%s>\n", tdp->fieldTag, tdp->dataPiece, tdp->fieldTag);
				}
			}

			curr_ptr = curr_ptr->next_ptr;
			i++;
		}

		if (totalRows > 0 || dataFound) {
			esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "</row>\n");
		}
		esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "</result>\n");
	}
	/* these are tables created out of datavault keys */
	else if (objDataType == ESIF_DATA_STRING) {
		esif_ccb_strcpy(output, "<result>\n", OUT_BUF_LEN);

		data_nspace = EsifData_Create();
		data_key = EsifData_Create();
		
		if (data_nspace == NULL || data_key == NULL) {
			rc = ESIF_E_NO_MEMORY;
			goto exit;
		}

		for (i = 0; i < self->numFields; i++) {
			char keyname[MAX_TABLEOBJECT_KEY_LEN] = { 0 };
			EsifConfigFindContext context = NULL;

			if (self->fields[i].dataVaultKey == NULL) {
				rc = ESIF_E_NOT_IMPLEMENTED;
				goto exit;
			}

			esif_ccb_sprintf(MAX_TABLEOBJECT_KEY_LEN, keyname, "%s/*", self->fields[i].dataVaultKey);
			EsifData_Set(data_nspace, ESIF_DATA_STRING, (void *) (self->dataSource ? self->dataSource : "OVERRIDE"), 0, ESIFAUTOLEN);
			EsifData_Set(data_key, ESIF_DATA_STRING, keyname, 0, ESIFAUTOLEN);

			if ((rc = EsifConfigFindFirst(data_nspace, data_key, NULL, &context)) == ESIF_OK) {
				do {
					EsifDataPtr  data_value = EsifData_CreateAs(self->fields[i].dataType, NULL, ESIF_DATA_ALLOCATE, 0);
					if (data_value == NULL) {
						EsifConfigFindClose(&context);
						rc = ESIF_E_NO_MEMORY;
						goto exit;
					}
					rc = EsifConfigGet(data_nspace, data_key, data_value);
					if (rc == ESIF_OK) { //these only have a single field until there is a need to span across keys
						esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "  <tableRow>\n");
						esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "    <%s>%s</%s>\n", self->fields[i].name, data_value->buf_ptr, self->fields[i].name);
						esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "  </tableRow>\n");
					}
					EsifData_Set(data_key, ESIF_DATA_STRING, keyname, 0, ESIFAUTOLEN);
					EsifData_Destroy(data_value);
				} while ((rc = EsifConfigFindNext(data_nspace, data_key, NULL, &context)) == ESIF_OK);

				EsifConfigFindClose(&context);
				if (rc == ESIF_E_ITERATION_DONE)
					rc = ESIF_OK;
			}
		}
		
		esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "</result>\n");
	}
	/* these are virtual tables (collections of individual primitives, grouped together to form
	a result set */
	else {
		esif_ccb_strcpy(output, "<result>\n", OUT_BUF_LEN);
		esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "  <tableRow>\n");
		for (i = 0; i < self->numFields; i++) {
			int32FieldValue = 0;
			if (self->fields[i].getPrimitive > 0) {
				response.type = self->fields[i].dataType;
				switch (self->fields[i].dataType) {
				case ESIF_DATA_STRING:
					strFieldValue = "";
					esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "    <%s>%s</%s>\n", self->fields[i].name, strFieldValue, self->fields[i].name);
					break;
				case ESIF_DATA_UINT32:
				case ESIF_DATA_POWER:
				case ESIF_DATA_TIME:
					response.buf_ptr = &defaultNumber;
					response.buf_len = sizeof(defaultNumber);
					primitiveOK = EsifExecutePrimitive((UInt8) self->participantId, self->fields[i].getPrimitive, self->domainQualifier, self->fields[i].instance, &request, &response);
					if (ESIF_OK == primitiveOK) {
						int32FieldValue = *(UInt32 *) response.buf_ptr;
					}
					esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "    <%s>%u</%s>\n", self->fields[i].name, int32FieldValue, self->fields[i].name);
					break;
				case ESIF_DATA_TEMPERATURE:
					int32FieldValue = 0xFFFFFFFF;

					response.buf_ptr = &defaultNumber;
					response.buf_len = sizeof(defaultNumber);
					primitiveOK = EsifExecutePrimitive((UInt8) self->participantId, self->fields[i].getPrimitive, self->domainQualifier, self->fields[i].instance, &request, &response);
					if (ESIF_OK != primitiveOK) {
						break;
					}
					int32FieldValue = *(UInt32 *)response.buf_ptr;
					switch(tempXformType){
					case ESIF_TEMP_C:
						esif_convert_temp(NORMALIZE_TEMP_TYPE, ESIF_TEMP_DECIC, &int32FieldValue);
						esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "    <%s>%.1f</%s>\n",
							self->fields[i].name,
							(float)(int)int32FieldValue / 10.0,
							self->fields[i].name);
						break;
					case ESIF_TEMP_K:
						esif_convert_temp(NORMALIZE_TEMP_TYPE, ESIF_TEMP_DECIK, &int32FieldValue);
						esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "    <%s>%.1f</%s>\n",
							self->fields[i].name,
							(float)(int)int32FieldValue / 10.0,
							self->fields[i].name);
						break;
					default:
						esif_convert_temp(NORMALIZE_TEMP_TYPE, tempXformType, &int32FieldValue);
						esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "    <%s>%u</%s>\n",
							self->fields[i].name,
							int32FieldValue,
							self->fields[i].name);
						break;
					}
					break;
				case ESIF_DATA_STRUCTURE:
					response.buf_ptr = NULL;
					response.buf_len = ESIF_DATA_ALLOCATE;
					response.type = ESIF_DATA_AUTO;
					primitiveOK = EsifExecutePrimitive((UInt8) self->participantId, self->fields[i].getPrimitive, self->domainQualifier, self->fields[i].instance, &request, &response);
					if (ESIF_OK == primitiveOK && response.buf_ptr) {
						struct esif_data_binary_fst_package *fst_ptr = (struct esif_data_binary_fst_package *)response.buf_ptr;
						struct esif_data_binary_bst_package *bst_ptr = (struct esif_data_binary_bst_package *)response.buf_ptr;

						switch (self->fields[i].getPrimitive) {
						case GET_FAN_STATUS:
							esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "    <%s>%u</%s>\n", self->fields[i].name, (u32) fst_ptr->speed.integer.value, self->fields[i].name);
							break;
						case GET_BATTERY_STATUS:
							esif_ccb_sprintf_concat(OUT_BUF_LEN, output,
								"    <batteryState>%u</batteryState>\n"
								"    <batteryRate>%u</batteryRate>\n"
								"    <batteryCapacity>%u</batteryCapacity>\n"
								"    <batteryVoltage>%u</batteryVoltage>\n",
								(u32) bst_ptr->battery_state.integer.value,
								(u32) bst_ptr->battery_present_rate.integer.value,
								(u32) bst_ptr->battery_remaining_capacity.integer.value,
								(u32) bst_ptr->battery_present_voltage.integer.value);
							break;
						case GET_RAPL_POWER_CONTROL_CAPABILITIES:
							remain_bytes = response.data_len;
							union esif_data_variant *data_ptr = response.buf_ptr;
							if (remain_bytes > sizeof(*data_ptr))
							{
								// skip revision
								remain_bytes -= sizeof(*data_ptr);
								data_ptr = (union esif_data_variant *)((u8 *)data_ptr + sizeof(*data_ptr));
							}
							
							struct esif_data_binary_ppcc_package *ppcc_ptr = (struct esif_data_binary_ppcc_package *)(u8 *)data_ptr;
							u32 ppccIndex = 0;
							while (remain_bytes >= sizeof(struct esif_data_binary_ppcc_package))
							{
								ppccIndex = (u32)ppcc_ptr->pl_index.integer.value + 1;
								esif_ccb_sprintf_concat(OUT_BUF_LEN, output,
									"    <pl%uMin>%u</pl%uMin>\n"
									"    <pl%uMax>%u</pl%uMax>\n"
									"    <pl%uTimeWindowMin>%u</pl%uTimeWindowMin>\n"
									"    <pl%uTimeWindowMax>%u</pl%uTimeWindowMax>\n"
									"    <pl%uStepSize>%u</pl%uStepSize>\n",
									ppccIndex,
									(u32)ppcc_ptr->pl_min.integer.value,
									ppccIndex,
									ppccIndex,
									(u32)ppcc_ptr->pl_max.integer.value,
									ppccIndex,
									ppccIndex,
									(u32)ppcc_ptr->time_window_min.integer.value,
									ppccIndex,
									ppccIndex,
									(u32)ppcc_ptr->time_window_max.integer.value,
									ppccIndex,
									ppccIndex,
									(u32)ppcc_ptr->step_size.integer.value,
									ppccIndex);

								remain_bytes -= sizeof(struct esif_data_binary_ppcc_package);
								ppcc_ptr = (struct esif_data_binary_ppcc_package *)((u8 *)ppcc_ptr + sizeof(*ppcc_ptr));
							}
							break;
						case GET_OEM_VARS:
							remain_bytes = response.data_len;
							union esif_data_variant *odvp_ptr = response.buf_ptr;
							u32 cnt = 0;
							while (remain_bytes >= sizeof(*odvp_ptr))
							{
								if (odvp_ptr->type == ESIF_DATA_UINT64 || odvp_ptr->type == ESIF_DATA_UINT32)
								{
									esif_ccb_sprintf_concat(OUT_BUF_LEN, output,
										"    <oem%u>%u</oem%u>\n",
										cnt, odvp_ptr->integer.value, cnt);
								}

								++cnt;
								remain_bytes -= sizeof(union esif_data_variant);
								odvp_ptr = (union esif_data_variant *)((u8 *)odvp_ptr + sizeof(*odvp_ptr));
							}
							break;
						default:
							//do nothing
							break;
						}
					}
					esif_ccb_free(response.buf_ptr);
					break;
				default:
					ESIF_TRACE_DEBUG("Field type in schema for table %s is not handled \n", self->name);
					rc = ESIF_E_PARAMETER_IS_OUT_OF_BOUNDS;
					goto exit;
					break;
				}
			}
		}
		esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "  </tableRow>\n");
		esif_ccb_sprintf_concat(OUT_BUF_LEN, output, "</result>\n");
	}
	self->dataXML = esif_ccb_strdup(output);

exit:
	esif_link_list_free_data_and_destroy(TableDataListPtr, NULL);
	esif_ccb_free(output);
	esif_ccb_free(tmp_buf);
	EsifData_Destroy(data_nspace);
	EsifData_Destroy(data_key);
	return rc;
}

eEsifError TableObject_Convert(
	TableObject *self
	)
{
	u8 *tableMem = NULL;
	u8	*binaryOutput = NULL;
	u8  *new_tableMem = NULL;
	char *tableColValue = NULL;  //used to enforce column size and ensure null terminator
	u32 totalBytesNeeded = 0;
	u32 lengthNumber = 0;	/* For parsing IDSP only */
	u64 colValueNumber = 0;
	u64 binaryCounter = 0;  /* maintains offset for repositioning binaryOutput every realloc */
	int i = 0;
	int numFields = 0;
	char *textInput;
	EsifDataType objDataType;
	char *tableRow = NULL;
	char *rowTok = NULL;
	char *tableCol = NULL;
	char *tmp;
	char *colTok = NULL;
	char rowDelims [] = "!";
	char colDelims [] = ",";
	eEsifError rc = ESIF_OK;
	EsifDataType targetType;
	struct guid_t guid = { 0 };
	UInt16 guidShorts[8] = {0};

	textInput = self->dataText;
	numFields = self->numFields;
	objDataType = self->dataType;
	totalBytesNeeded = 0;

	u32 stringType = ESIF_DATA_STRING;
	u32 numberType = ESIF_DATA_UINT64;
	u32 binaryType = ESIF_DATA_BINARY;
	char revisionNumString[REVISION_INDICATOR_LENGTH + 1];
	char modeString[MODE_INDICATOR_LENGTH + 1];
	u64 revisionNumInt = 0;
	u64 modeNumInt = 0;


	/* if the table expects a revision, the input string should be
	<revision number>:<data>, with the revision number occupying a
	char length of REVISION_INDICATOR_LENGTH */
	/* if the table expects a revision and mode, the input string should be
	<revision number>:<mode number>:<data>, with the revision number occupying a
	char length of REVISION_INDICATOR_LENGTH and the mode number occupying a
	char length of MODE_INDICATOR_LENGTH*/
	if (FLAGS_TEST(self->options, TABLEOPT_CONTAINS_REVISION) && esif_ccb_strlen(textInput, REVISION_INDICATOR_LENGTH + 1) > REVISION_INDICATOR_LENGTH) {
		esif_ccb_memcpy(&revisionNumString, textInput, REVISION_INDICATOR_LENGTH);
		revisionNumString[REVISION_INDICATOR_LENGTH] = '\0';

		if (FLAGS_TEST(self->options, TABLEOPT_CONTAINS_MODE)) {
			textInput += MODE_INDICATOR_LENGTH + 1;
			esif_ccb_memcpy(&modeString, textInput, MODE_INDICATOR_LENGTH);
			modeString[MODE_INDICATOR_LENGTH] = '\0';
			modeNumInt = esif_atoi(modeString);
			totalBytesNeeded += sizeof(numberType) + sizeof(modeNumInt);
		}

		textInput += REVISION_INDICATOR_LENGTH + 1;
		revisionNumInt = esif_atoi(revisionNumString);
		totalBytesNeeded += sizeof(numberType) + sizeof(revisionNumInt);
		tableMem = (u8*)esif_ccb_malloc(totalBytesNeeded);
		if (tableMem == NULL) {
			rc = ESIF_E_NO_MEMORY;
			goto exit;
		}

		binaryOutput = tableMem;
		esif_ccb_memcpy(binaryOutput, &numberType, sizeof(numberType));
		binaryOutput += sizeof(numberType);
		binaryCounter += sizeof(numberType);
		esif_ccb_memcpy(binaryOutput, &revisionNumInt, sizeof(revisionNumInt));
		binaryOutput += sizeof(revisionNumInt);
		binaryCounter += sizeof(revisionNumInt);
		if (FLAGS_TEST(self->options, TABLEOPT_CONTAINS_MODE)) {
			esif_ccb_memcpy(binaryOutput, &numberType, sizeof(numberType));
			binaryOutput += sizeof(numberType);
			binaryCounter += sizeof(numberType);
			esif_ccb_memcpy(binaryOutput, &modeNumInt, sizeof(modeNumInt));
			binaryOutput += sizeof(modeNumInt);
			binaryCounter += sizeof(modeNumInt);
		}
	}

	tableRow = esif_ccb_strtok(textInput, rowDelims, &rowTok);

	while (tableRow != NULL) {
		i = -1;
		tableCol = esif_ccb_strtok(tableRow, colDelims, &colTok);
		while (tableCol != NULL) {
			u64 colValueLen = esif_ccb_strlen(tableCol, COLUMN_MAX_SIZE - 1) + 1;
			tableColValue = (char *) esif_ccb_malloc((size_t)colValueLen);
			if (tableColValue == NULL) {
				rc = ESIF_E_NO_MEMORY;
				goto exit;
			}
			esif_ccb_strcpy(tableColValue, tableCol, (size_t)colValueLen);
			i++;
			if (i < numFields || self->dynamicColumnCount) {
				if ((tmp = esif_ccb_strstr(tableColValue, "'")) != NULL) {
					*tmp = 0;
				}

				if (FLAGS_TEST(self->options, TABLEOPT_ALLOW_SELF_DEFINE)) {
					targetType = (esif_atoi(tableColValue) > 0 || strcmp(tableColValue, "0") == 0) ? ESIF_DATA_UINT64 : ESIF_DATA_STRING;
				}
				else {
					targetType = self->fields[i].dataType;
				}
				switch (targetType) {
				case ESIF_DATA_STRING:
					totalBytesNeeded += sizeof(stringType) + (u32)sizeof(colValueLen) + (u32) colValueLen;
					new_tableMem = (u8*) esif_ccb_realloc(tableMem, totalBytesNeeded);
					if (new_tableMem == NULL) {
						rc = ESIF_E_NO_MEMORY;
						goto exit;
					}
					tableMem = new_tableMem;
					binaryOutput = tableMem;
					binaryOutput += binaryCounter;
					esif_ccb_memcpy(binaryOutput, &stringType, sizeof(stringType));
					binaryOutput += sizeof(stringType);
					binaryCounter += sizeof(stringType);
					esif_ccb_memcpy(binaryOutput, &colValueLen, sizeof(colValueLen));
					binaryOutput += sizeof(colValueLen);
					binaryCounter += sizeof(colValueLen);
					esif_ccb_memcpy(binaryOutput, tableColValue, (size_t) colValueLen);
					binaryOutput += colValueLen;
					binaryCounter += colValueLen;
					break;
				case ESIF_DATA_UINT32:	// UInt32 and UInt64 treated equally because bios field is always 64 for number
				case ESIF_DATA_UINT64:
					colValueNumber = esif_atoi(tableColValue);
					totalBytesNeeded += sizeof(numberType) + (u32)sizeof(colValueNumber);
					new_tableMem = (u8*) esif_ccb_realloc(tableMem, totalBytesNeeded);
					if (new_tableMem == NULL) {
						rc = ESIF_E_NO_MEMORY;
						goto exit;
					}
					tableMem = new_tableMem;
					binaryOutput = tableMem;
					binaryOutput += binaryCounter;
					esif_ccb_memcpy(binaryOutput, &numberType, sizeof(numberType));
					binaryOutput += sizeof(numberType);
					binaryCounter += sizeof(numberType);
					esif_ccb_memcpy(binaryOutput, &colValueNumber, sizeof(colValueNumber));
					binaryOutput += sizeof(colValueNumber);
					binaryCounter += sizeof(colValueNumber);
					break;
				case ESIF_DATA_BINARY:
					lengthNumber = esif_atoi(tableColValue);  // First field is length of binary data
					if (lengthNumber > MAX_TABLEOBJECT_BINARY) {
						rc = ESIF_E_PARAMETER_IS_OUT_OF_BOUNDS;
						goto exit;
					}
					u32 reserved = 0;                      // Binary Type implies a 4-byte reserved field after length
					totalBytesNeeded += sizeof(binaryType) + sizeof(lengthNumber) + sizeof(reserved) + lengthNumber;
					new_tableMem = (u8*) esif_ccb_realloc(tableMem, totalBytesNeeded);
					if (new_tableMem == NULL) {
						rc = ESIF_E_NO_MEMORY;
						goto exit;
					}
					tableMem = new_tableMem;
					binaryOutput = tableMem + binaryCounter;
					esif_ccb_memcpy(binaryOutput, &binaryType, sizeof(binaryType));  // Type
					binaryOutput += sizeof(binaryType);
					esif_ccb_memcpy(binaryOutput, &lengthNumber, sizeof(lengthNumber));  // Length
					binaryOutput += sizeof(lengthNumber);
					esif_ccb_memcpy(binaryOutput, &reserved, sizeof(reserved));  // Reserved
					binaryOutput += sizeof(reserved);
					// Must get the next column here (the UUID) because it is part of a structure
					tableCol = esif_ccb_strtok(NULL, colDelims, &colTok);
					
					if (tableCol) {
						//
						// "hhx" is not understood by Windows, so must first read into shorts and
						// then copy to bytes to resolve static analysis issues.
						//
						esif_ccb_sscanf(tableCol, "%8x-%4hx-%4hx-%2hx%2hx-%2hx%2hx%2hx%2hx%2hx%2hx", 
						&guid.data1, &guid.data2, &guid.data3, 
						&guidShorts[0], &guidShorts[1], &guidShorts[2], &guidShorts[3],
						&guidShorts[4], &guidShorts[5], &guidShorts[6], &guidShorts[7]);
						esif_copy_shorts_to_bytes(guid.data4, guidShorts, 8);
						esif_ccb_memcpy(binaryOutput, &guid, sizeof(guid));
						binaryOutput += sizeof(guid);
					}
					binaryCounter = binaryOutput - tableMem;

					break;
				default:
					ESIF_TRACE_DEBUG("Field type in schema for table %s is not handled \n", self->name);
					rc = ESIF_E_PARAMETER_IS_OUT_OF_BOUNDS;
					goto exit;
					break;
				}
			}

			esif_ccb_free(tableColValue);
			tableColValue = NULL;

			// Get next column
			tableCol = esif_ccb_strtok(NULL, colDelims, &colTok);
		}

		// Get next row
		tableRow = esif_ccb_strtok(NULL, rowDelims, &rowTok);
	}

	if (totalBytesNeeded) {
		self->binaryData = esif_ccb_malloc(totalBytesNeeded);
		if (self->binaryData == NULL) {
			rc = ESIF_E_NO_MEMORY;
			goto exit;
		}
		esif_ccb_memcpy((u8 *) self->binaryData, tableMem, totalBytesNeeded);
		self->binaryDataSize = totalBytesNeeded;
	}
	else {
		rc = ESIF_E_UNSPECIFIED;
	}

exit:
	esif_ccb_free(tableMem);
	esif_ccb_free(tableColValue);
	return rc;
}

void TableObject_Construct(
	TableObject *self,
	char *dataName,
	char *domainQualifier,
	char *dataSource,
	char *dataMember,
	char *dataText,
	int participantId,
	enum tableMode mode
	)
{
	esif_ccb_memset(self, 0, sizeof(*self));
	if (dataName != NULL) {
		self->name = esif_ccb_strdup(dataName);
	}
	if (domainQualifier != NULL) {
		self->domainQualifier = esif_ccb_strdup(domainQualifier);
	}
	if (dataSource != NULL) {
		self->dataSource = esif_ccb_strdup(dataSource);
	}
	if (dataMember != NULL) {
		self->dataMember = esif_ccb_strdup(dataMember);
	}
	if (dataText != NULL) {
		self->dataText = esif_ccb_strdup(dataText);
	}
	self->participantId = participantId;

	self->mode = mode;
}

eEsifError TableObject_Delete(
	TableObject *self
	)
{
	char *namesp = ESIF_DSP_OVERRIDE_NAMESPACE;
	eEsifError rc = ESIF_OK;
	EsifDataPtr  data_nspace = NULL;
	EsifDataPtr  data_key = NULL;
	UInt16 domain = ESIF_PRIMITIVE_DOMAIN_D0;
	EsifDataType objDataType = ESIF_DATA_BINARY;
	EsifUpPtr upPtr = NULL;

	ESIF_ASSERT(self != NULL);

	objDataType = self->dataType;

	if (esif_ccb_strlen(self->domainQualifier, sizeof(UInt16)) < sizeof(UInt16)) {
		rc = ESIF_E_PARAMETER_IS_NULL;
		goto exit;
	}
	domain = domain_str_to_short(self->domainQualifier);

	/* 
	 * First determine if this is a binary table 
	 * or a virtual table 
	 */
	if (objDataType == ESIF_DATA_BINARY) {

		if (self->dataMember == NULL) {
			rc = ESIF_E_NOT_IMPLEMENTED;
			goto exit;
		}

		TableObject_ResetConfig(self, self->setPrimitive, ESIF_NO_PERSIST_INSTANCE);
		rc = TableObject_ResetConfig(self, self->setPrimitive, ESIF_NO_INSTANCE);

		EsifEventMgr_SignalEvent((UInt8) self->participantId, domain, self->changeEvent, NULL);
	}
	else {  //virtual tables
		int i = 0;
		upPtr = EsifUpPm_GetAvailableParticipantByInstance((UInt8) self->participantId);
		if (NULL == upPtr) {
			rc = ESIF_E_PARTICIPANT_NOT_FOUND;
			goto exit;
		}
		data_nspace = EsifData_CreateAs(ESIF_DATA_STRING, namesp, 0, ESIFAUTOLEN);
		if (data_nspace == NULL) {
			rc = ESIF_E_NO_MEMORY;
			goto exit;
		}
		for (i = 0; i < self->numFields; i++) {
			char targetKey[MAX_TABLEOBJECT_KEY_LEN] = { 0 };
			EsifDataPtr targetDataKey = NULL;
			
			if (self->fields[i].dataVaultKey != NULL) {
				esif_ccb_strcpy(targetKey, self->fields[i].dataVaultKey, MAX_TABLEOBJECT_KEY_LEN);
			}
			else if (self->dataVaultCategory == NULL) {
				esif_ccb_sprintf(MAX_TABLEOBJECT_KEY_LEN, targetKey,
					"/participants/%s.%s/%.*s%s",
					EsifUp_GetName(upPtr), self->domainQualifier,
					(int) (ACPI_NAME_TARGET_SIZE - esif_ccb_min(esif_ccb_strlen(self->name, ACPI_NAME_TARGET_SIZE), ACPI_NAME_TARGET_SIZE)), "____",
					self->fields[i].name);
			}
			else {
				esif_ccb_sprintf(MAX_TABLEOBJECT_KEY_LEN, targetKey,
					"/participants/%s.%s/%s/%.*s%s",
					EsifUp_GetName(upPtr), self->domainQualifier,self->dataVaultCategory,
					(int) (ACPI_NAME_TARGET_SIZE - esif_ccb_min(esif_ccb_strlen(self->name, ACPI_NAME_TARGET_SIZE), ACPI_NAME_TARGET_SIZE)), "____",
					self->fields[i].name);
				if (self->fields[i].instance != ESIF_NO_INSTANCE)
				{
					TableObject_ResetConfig(self, self->fields[i].setPrimitive, self->fields[i].instance + ESIF_NO_PERSIST_OFFSET);
				}
				else
				{
					TableObject_ResetConfig(self, self->fields[i].setPrimitive, ESIF_NO_PERSIST_INSTANCE);
				}
				TableObject_ResetConfig(self, self->fields[i].setPrimitive, self->fields[i].instance);
			}

			targetDataKey = EsifData_CreateAs(ESIF_DATA_STRING, targetKey, 0, ESIFAUTOLEN);

			if (targetDataKey == NULL) {
				continue;
			}

			/* 
			 * Best effort depending on the fact that there 
			 * are field keys in the datavault
			 */
			TableObject_ResetConfig(self, self->setPrimitive, ESIF_NO_PERSIST_INSTANCE);
			rc = EsifConfigDelete(data_nspace, targetDataKey);
			/* 
			 * Send event regardless of rc code to allow flexibility in the return value, and
			 * no harm is done by the event
			 */
			if (self->changeEvent != 0)
			{
				EsifEventMgr_SignalEvent((UInt8)self->participantId, domain, self->changeEvent, NULL);
			}
			EsifData_Destroy(targetDataKey);
		}
	}

exit:
	if (upPtr != NULL) {
		EsifUp_PutRef(upPtr);
	}
	EsifData_Destroy(data_nspace);
	EsifData_Destroy(data_key);
	return rc;
}


eEsifError TableObject_LoadAttributes(
	TableObject *self
	)
{
	eEsifError rc = ESIF_OK;
	EsifUpPtr upPtr = NULL;
	char targetKey[MAX_TABLEOBJECT_KEY_LEN] = { 0 };
	
	upPtr = EsifUpPm_GetAvailableParticipantByInstance((UInt8) self->participantId);
	if (NULL == upPtr) {
		rc = ESIF_E_PARTICIPANT_NOT_FOUND;
		goto exit;
	}
	if (NULL == self->name) {
		rc = ESIF_E_PARAMETER_IS_NULL;
		goto exit;
	}

	/* replace with lookup */
	if (esif_ccb_stricmp(self->name, "trt") == 0) {
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_THERMAL_RELATIONSHIP_TABLE;
		self->setPrimitive = SET_THERMAL_RELATIONSHIP_TABLE;
		self->changeEvent = ESIF_EVENT_APP_THERMAL_RELATIONSHIP_CHANGED;
	}
	else if (esif_ccb_stricmp(self->name, "art") == 0) {
		FLAGS_SET(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_CLEAR(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_ACTIVE_RELATIONSHIP_TABLE;
		self->setPrimitive = SET_ACTIVE_RELATIONSHIP_TABLE;
		self->changeEvent = ESIF_EVENT_APP_ACTIVE_RELATIONSHIP_CHANGED;
	}
	else if (esif_ccb_stricmp(self->name, "bcl") == 0) {
		FLAGS_CLEAR(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_CLEAR(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_DISPLAY_BRIGHTNESS_LEVELS;
		self->setPrimitive = SET_DISPLAY_BRIGHTNESS_LEVELS;
		self->changeEvent = ESIF_EVENT_DOMAIN_DISPLAY_CAPABILITY_CHANGED;
	}
	else if (esif_ccb_stricmp(self->name, "odvp") == 0) {
		FLAGS_CLEAR(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_CLEAR(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_OEM_VARS;
		self->setPrimitive = SET_OEM_VARS;
		self->changeEvent = ESIF_EVENT_OEM_VARS_CHANGED;
	}
	else if (esif_ccb_stricmp(self->name, "psvt") == 0) {
		FLAGS_SET(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_SET(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_PASSIVE_RELATIONSHIP_TABLE;
		self->setPrimitive = SET_PASSIVE_RELATIONSHIP_TABLE;
		self->changeEvent = ESIF_EVENT_PASSIVE_TABLE_CHANGED;
	}
	else if (esif_ccb_stricmp(self->name, "apct") == 0) {
		FLAGS_SET(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_SET(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_ADAPTIVE_PERFORMANCE_CONDITIONS_TABLE;
		self->setPrimitive = SET_ADAPTIVE_PERFORMANCE_CONDITIONS_TABLE;
		self->changeEvent = ESIF_EVENT_ADAPTIVE_PERFORMANCE_CONDITIONS_TABLE_CHANGED;
	}
	else if (esif_ccb_stricmp(self->name, "apat") == 0) {

		FLAGS_SET(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_CLEAR(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_ADAPTIVE_PERFORMANCE_ACTIONS_TABLE;
		self->setPrimitive = SET_ADAPTIVE_PERFORMANCE_ACTIONS_TABLE;
		self->changeEvent = ESIF_EVENT_ADAPTIVE_PERFORMANCE_ACTIONS_TABLE_CHANGED;
	}
	else if (esif_ccb_stricmp(self->name, "appc") == 0) {

		FLAGS_SET(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_CLEAR(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_ADAPTIVE_PERFORMANCE_PARTICIPANT_CONDITION_TABLE;
		self->setPrimitive = SET_ADAPTIVE_PERFORMANCE_PARTICIPANT_CONDITION_TABLE;
		self->changeEvent = ESIF_EVENT_ADAPTIVE_PERFORMANCE_PARTICIPANT_CONDITION_TABLE_CHANGED;
	}
	else if (esif_ccb_stricmp(self->name, "pbct") == 0) {
		FLAGS_SET(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_SET(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_POWER_BOSS_CONDITIONS_TABLE;
		self->setPrimitive = SET_POWER_BOSS_CONDITIONS_TABLE;
		self->changeEvent = ESIF_EVENT_POWER_BOSS_CONDITIONS_TABLE_CHANGED;
	}
	else if (esif_ccb_stricmp(self->name, "pbat") == 0) {

		FLAGS_SET(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_CLEAR(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_POWER_BOSS_ACTIONS_TABLE;
		self->setPrimitive = SET_POWER_BOSS_ACTIONS_TABLE;
		self->changeEvent = ESIF_EVENT_POWER_BOSS_ACTIONS_TABLE_CHANGED;
	}
	else if (esif_ccb_stricmp(self->name, "pbmt") == 0) {
		FLAGS_SET(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_CLEAR(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_POWER_BOSS_MATH_TABLE;
		self->setPrimitive = SET_POWER_BOSS_MATH_TABLE;
		self->changeEvent = ESIF_EVENT_POWER_BOSS_MATH_TABLE_CHANGED;
	}
	else if (esif_ccb_stricmp(self->name, "idsp") == 0) {
		FLAGS_CLEAR(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_SET(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_SUPPORTED_POLICIES;
		self->setPrimitive = 0;
		self->changeEvent = ESIF_EVENT_PARTICIPANT_SPEC_INFO_CHANGED;
	}
	else if (esif_ccb_stricmp(self->name, "ppcc") == 0) {
		FLAGS_SET(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_CLEAR(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_RAPL_POWER_CONTROL_CAPABILITIES;
		self->setPrimitive = SET_RAPL_POWER_CONTROL_CAPABILITIES;
		self->changeEvent = ESIF_EVENT_DOMAIN_POWER_CAPABILITY_CHANGED;
	}
	else if (esif_ccb_stricmp(self->name, "vsct") == 0) {
		FLAGS_SET(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_CLEAR(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_VIRTUAL_SENSOR_CALIB_TABLE;
		self->setPrimitive = SET_VIRTUAL_SENSOR_CALIB_TABLE;
		self->changeEvent = ESIF_EVENT_VIRTUAL_SENSOR_CALIB_TABLE_CHANGED;
	}
	else if (esif_ccb_stricmp(self->name, "vspt") == 0) {
		FLAGS_SET(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_CLEAR(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_VIRTUAL_SENSOR_POLLING_TABLE;
		self->setPrimitive = SET_VIRTUAL_SENSOR_POLLING_TABLE;
		self->changeEvent = ESIF_EVENT_VIRTUAL_SENSOR_POLLING_TABLE_CHANGED;
	}
	else if (esif_ccb_stricmp(self->name, "ppss") == 0) {
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_PERF_SUPPORT_STATES;
		self->setPrimitive = SET_PERF_SUPPORT_STATES;
		self->changeEvent = ESIF_EVENT_DOMAIN_PERF_CONTROL_CHANGED;
	}
	else if (esif_ccb_stricmp(self->name, "pss") == 0) {
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_PROC_PERF_SUPPORT_STATES;
		self->setPrimitive = SET_PROC_PERF_SUPPORT_STATES;
		self->changeEvent = ESIF_EVENT_DOMAIN_PERF_CONTROL_CHANGED;
	}
	else if (esif_ccb_stricmp(self->name, "psv") == 0) {
		self->dataType = ESIF_DATA_UINT32;
		self->getPrimitive = 0;  /* NA for virtual tables */
		self->setPrimitive = 0;
		self->changeEvent = ESIF_EVENT_PARTICIPANT_SPEC_INFO_CHANGED;
		self->dataVaultCategory = esif_ccb_strdup("trippoint");
	}
	else if (esif_ccb_stricmp(self->name, "status") == 0) {
		self->dataType = ESIF_DATA_UINT32;
		self->getPrimitive = 0;  /* NA for virtual tables */
		self->setPrimitive = 0;
		self->changeEvent = 0;
	}
	else if (esif_ccb_stricmp(self->name, "workload") == 0) {
		self->dataType = ESIF_DATA_STRING;
		self->getPrimitive = 0;  /* NA for virtual tables */
		self->setPrimitive = 0;
		self->changeEvent = 0;
		self->dataSource = esif_ccb_strdup("DPTF");
		self->dataMember = esif_ccb_strdup("/shared/export/workload_hints/*");
	}
	else if (esif_ccb_stricmp(self->name, "participant_min") == 0) {
		self->dataType = ESIF_DATA_UINT32;
		self->getPrimitive = 0;  /* NA for virtual tables */
		self->setPrimitive = 0;
		self->changeEvent = 0;
	}
	else if (esif_ccb_stricmp(self->name, "pdl") == 0) {
		self->dataType = ESIF_DATA_UINT32;
		self->getPrimitive = 0;  /* NA for virtual tables */
		self->setPrimitive = 0;
		self->changeEvent = 0;
	}
	else if (esif_ccb_stricmp(self->name, "ppdl") == 0) {
		self->dataType = ESIF_DATA_UINT32;
		self->getPrimitive = 0;  /* NA for virtual tables */
		self->setPrimitive = 0;
		self->changeEvent = 0;
	}
	else if (esif_ccb_stricmp(self->name, "ecmt") == 0) {
		FLAGS_SET(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_CLEAR(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_EMERGENCY_CALL_MODE_TABLE;
		self->setPrimitive = SET_EMERGENCY_CALL_MODE_TABLE;
		self->changeEvent = ESIF_EVENT_EMERGENCY_CALL_MODE_TABLE_CHANGED;
	}
	else if (esif_ccb_stricmp(self->name, "pida") == 0) {
		FLAGS_SET(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_CLEAR(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_PID_ALGORITHM_TABLE;
		self->setPrimitive = SET_PID_ALGORITHM_TABLE;
		self->changeEvent = ESIF_EVENT_PID_ALGORITHM_TABLE_CHANGED;
	}
	else if (esif_ccb_stricmp(self->name, "acpr") == 0) {
		FLAGS_SET(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_CLEAR(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		FLAGS_SET(self->options, TABLEOPT_CONTAINS_MODE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_ACTIVE_CONTROL_POINT_RELATIONSHIP_TABLE;
		self->setPrimitive = SET_ACTIVE_CONTROL_POINT_RELATIONSHIP_TABLE;
		self->changeEvent = ESIF_EVENT_ACTIVE_CONTROL_POINT_RELATIONSHIP_TABLE_CHANGED;
	}
	else if (esif_ccb_stricmp(self->name, "psha") == 0) {
		FLAGS_SET(self->options, TABLEOPT_CONTAINS_REVISION);
		FLAGS_SET(self->options, TABLEOPT_ALLOW_SELF_DEFINE);
		self->dataType = ESIF_DATA_BINARY;
		self->getPrimitive = GET_POWER_SHARING_ALGORITHM_TABLE;
		self->setPrimitive = SET_POWER_SHARING_ALGORITHM_TABLE;
		self->changeEvent = ESIF_EVENT_POWER_SHARING_ALGORITHM_TABLE_CHANGED;
	}
	else {
		rc = ESIF_E_NOT_IMPLEMENTED;
	}

	/* Construct DataVault Key */
	if (self->dataMember == NULL) {
		esif_ccb_sprintf(MAX_TABLEOBJECT_KEY_LEN, targetKey,
			"/participants/%s.%s/%.*s%s",
			EsifUp_GetName(upPtr), self->domainQualifier,
			(int) (ACPI_NAME_TARGET_SIZE - esif_ccb_min(esif_ccb_strlen(self->name, ACPI_NAME_TARGET_SIZE), ACPI_NAME_TARGET_SIZE)), "____",
			self->name);
		self->dataMember = esif_ccb_strdup(targetKey);
	}

exit:
	if (upPtr != NULL) {
		EsifUp_PutRef(upPtr);
	}
	return rc;
}


eEsifError TableObject_LoadSchema(
	TableObject *self
	)
{
	eEsifError rc = ESIF_OK;
	EsifUpPtr upPtr = NULL;
	char targetKey[MAX_TABLEOBJECT_KEY_LEN] = { 0 };
	TableField *fieldlist = NULL;

	upPtr = EsifUpPm_GetAvailableParticipantByInstance((UInt8) self->participantId);
	if (NULL == upPtr) {
		rc = ESIF_E_PARTICIPANT_NOT_FOUND;
		goto exit;
	}

	/* replace with lookup */
	if (esif_ccb_stricmp(self->name, "trt") == 0) {
		static TableField trt_fields [] = {
				{ "src", "source", ESIF_DATA_STRING },
				{ "dst", "destination", ESIF_DATA_STRING },
				{ "priority", "influence", ESIF_DATA_UINT64 },
				{ "sampleRate", "period", ESIF_DATA_UINT64 },
				{ "reserved0", "rsvd_0", ESIF_DATA_UINT64 },
				{ "reserved1", "rsvd_1", ESIF_DATA_UINT64 },
				{ "reserved2", "rsvd_2", ESIF_DATA_UINT64 },
				{ "reserved3", "rsvd_3", ESIF_DATA_UINT64 },
				{ 0 }
		};
		fieldlist = trt_fields;
	}
	else if (esif_ccb_stricmp(self->name, "art") == 0) {
		static TableField art_fields [] = {
				{ "src", "source", ESIF_DATA_STRING },
				{ "dst", "destination", ESIF_DATA_STRING },
				{ "priority", "influence", ESIF_DATA_UINT64 },
				{ "ac0", "ac0", ESIF_DATA_UINT64 },
				{ "ac1", "ac1", ESIF_DATA_UINT64 },
				{ "ac2", "ac2", ESIF_DATA_UINT64 },
				{ "ac3", "ac3", ESIF_DATA_UINT64 },
				{ "ac4", "ac4", ESIF_DATA_UINT64 },
				{ "ac5", "ac5", ESIF_DATA_UINT64 },
				{ "ac6", "ac6", ESIF_DATA_UINT64 },
				{ "ac7", "ac7", ESIF_DATA_UINT64 },
				{ "ac8", "ac8", ESIF_DATA_UINT64 },
				{ "ac9", "ac9", ESIF_DATA_UINT64 },
				{ 0 }
		};
		fieldlist = art_fields;
	}
	else if (esif_ccb_stricmp(self->name, "odvp") == 0) {
		static TableField odvp_fields [] = {
				{ "field",	"field", ESIF_DATA_UINT64 },
				{ 0 }
		};
		fieldlist = odvp_fields;
	}
	else if (esif_ccb_stricmp(self->name, "bcl") == 0) {
		static TableField bcl_fields [] = {
				{ "brightness", "brightness", ESIF_DATA_UINT64 },
				{ 0 }
		};
		fieldlist = bcl_fields;
	}
	else if (esif_ccb_stricmp(self->name, "psvt") == 0) {
		static TableField psvt_fields [] = {
				{ "fld1", "fld1", ESIF_DATA_STRING, 1 },
				{ "fld2", "fld2", ESIF_DATA_STRING },
				{ "fld3", "fld3", ESIF_DATA_UINT64 },
				{ "fld4", "fld4", ESIF_DATA_UINT64 },
				{ "fld5", "fld5", ESIF_DATA_UINT64 },
				{ "fld6", "fld6", ESIF_DATA_UINT64 },
				{ "fld7", "fld7", ESIF_DATA_UINT64 },
				{ "fld8", "fld8", ESIF_DATA_STRING },
				{ "fld9", "fld9", ESIF_DATA_UINT64 },
				{ "fld10", "fld10", ESIF_DATA_UINT64 },
				{ "fld11", "fld11", ESIF_DATA_UINT64 },
				{ "fld12", "fld12", ESIF_DATA_UINT64 },
				{ 0 }
		};
		fieldlist = psvt_fields;
	}
	else if ((esif_ccb_stricmp(self->name, "apct") == 0) || (esif_ccb_stricmp(self->name, "pbct") == 0)) {
		static TableField conditions_table_fields [] = {
				{ "fld1", "fld1", ESIF_DATA_UINT64 },
				{ "fld2", "fld2", ESIF_DATA_UINT64 },
				{ "fld3", "fld3", ESIF_DATA_UINT64 },
				{ "fld4", "fld4", ESIF_DATA_UINT64 },
				{ "fld5", "fld5", ESIF_DATA_UINT64 },
				{ "fld6", "fld6", ESIF_DATA_UINT64 },
				{ "fld7", "fld7", ESIF_DATA_UINT64 },
				{ "fld8", "fld8", ESIF_DATA_UINT64 },
				{ "fld9", "fld9", ESIF_DATA_UINT64 },
				{ "fld10", "fld10", ESIF_DATA_UINT64 },
				{ "fld11", "fld11", ESIF_DATA_UINT64 },
				{ "fld12", "fld12", ESIF_DATA_UINT64 },
				{ "fld13", "fld13", ESIF_DATA_UINT64 },
				{ "fld14", "fld14", ESIF_DATA_UINT64 },
				{ "fld15", "fld15", ESIF_DATA_UINT64 },
				{ "fld16", "fld16", ESIF_DATA_UINT64 },
				{ "fld17", "fld17", ESIF_DATA_UINT64 },
				{ "fld18", "fld18", ESIF_DATA_UINT64 },
				{ "fld19", "fld19", ESIF_DATA_UINT64 },
				{ "fld20", "fld20", ESIF_DATA_UINT64 },
				{ "fld21", "fld21", ESIF_DATA_UINT64 },
				{ "fld22", "fld22", ESIF_DATA_UINT64 },
				{ "fld23", "fld23", ESIF_DATA_UINT64 },
				{ "fld24", "fld24", ESIF_DATA_UINT64 },
				{ "fld25", "fld25", ESIF_DATA_UINT64 },
				{ "fld26", "fld26", ESIF_DATA_UINT64 },
				{ "fld27", "fld27", ESIF_DATA_UINT64 },
				{ "fld28", "fld28", ESIF_DATA_UINT64 },
				{ "fld29", "fld29", ESIF_DATA_UINT64 },
				{ "fld30", "fld30", ESIF_DATA_UINT64 },
				{ "fld31", "fld31", ESIF_DATA_UINT64 },
				{ "fld32", "fld32", ESIF_DATA_UINT64 },
				{ "fld33", "fld33", ESIF_DATA_UINT64 },
				{ "fld34", "fld34", ESIF_DATA_UINT64 },
				{ "fld35", "fld35", ESIF_DATA_UINT64 },
				{ "fld36", "fld36", ESIF_DATA_UINT64 },
				{ "fld37", "fld37", ESIF_DATA_UINT64 },
				{ "fld38", "fld38", ESIF_DATA_UINT64 },
				{ "fld39", "fld39", ESIF_DATA_UINT64 },
				{ "fld40", "fld40", ESIF_DATA_UINT64 },
				{ 0 }
		};
		fieldlist = conditions_table_fields;
	}
	else if (esif_ccb_stricmp(self->name, "apat") == 0 && self->version == 1) {
		static TableField actions_table_fields[] = {
				{ "fld1", "fld1", ESIF_DATA_UINT64 },
				{ "fld2", "fld2", ESIF_DATA_STRING },
				{ "fld3", "fld3", ESIF_DATA_STRING },
				{ "fld4", "fld4", ESIF_DATA_STRING },
				{ 0 }
		};
		fieldlist = actions_table_fields;
	}
	else if ((esif_ccb_stricmp(self->name, "pbat") == 0) || (esif_ccb_stricmp(self->name, "apat") == 0 && self->version == 2)) {
		static TableField actions_table_fields [] = {
				{ "fld1", "fld1", ESIF_DATA_UINT64 },
				{ "fld2", "fld2", ESIF_DATA_STRING },
				{ "fld3", "fld3", ESIF_DATA_STRING },
				{ "fld4", "fld4", ESIF_DATA_UINT64 },
				{ "fld5", "fld5", ESIF_DATA_STRING },
				{ "fld6", "fld6", ESIF_DATA_STRING },
				{ 0 }
		};
		fieldlist = actions_table_fields;
	}
	else if (esif_ccb_stricmp(self->name, "appc") == 0) {
		static TableField appc_table_fields[] = {
			{ "fld1", "fld1", ESIF_DATA_UINT64 },
			{ "fld2", "fld2", ESIF_DATA_STRING },
			{ "fld3", "fld3", ESIF_DATA_STRING },
			{ "fld4", "fld4", ESIF_DATA_UINT64 },
			{ "fld5", "fld5", ESIF_DATA_UINT64 },
			{ 0 }
		};
		fieldlist = appc_table_fields;
	}
	else if (esif_ccb_stricmp(self->name, "pbmt") == 0) {
		static TableField appc_table_fields[] = {
			{ "fld1", "fld1", ESIF_DATA_STRING },
			{ "fld2", "fld2", ESIF_DATA_UINT64 },
			{ "fld3", "fld3", ESIF_DATA_STRING },
			{ "fld4", "fld4", ESIF_DATA_STRING },
			{ "fld5", "fld5", ESIF_DATA_UINT64 },
			{ "fld6", "fld6", ESIF_DATA_STRING },
			{ "fld7", "fld7", ESIF_DATA_UINT64 },
			{ "fld8", "fld8", ESIF_DATA_UINT64 },
			{ "fld9", "fld9", ESIF_DATA_UINT64 },
			{ "fld10", "fld10", ESIF_DATA_STRING },
			{ "fld11", "fld11", ESIF_DATA_UINT64 },
			{ "fld12", "fld12", ESIF_DATA_UINT64 },
			{ "fld13", "fld13", ESIF_DATA_UINT64 },
			{ "fld14", "fld14", ESIF_DATA_UINT64 },
			{ "fld15", "fld15", ESIF_DATA_UINT64 },
			{ "fld16", "fld16", ESIF_DATA_UINT64 },
			{ "fld17", "fld17", ESIF_DATA_STRING },
			{ "fld18", "fld18", ESIF_DATA_UINT64 },
			{ "fld19", "fld19", ESIF_DATA_UINT64 },
			{ "fld20", "fld20", ESIF_DATA_UINT64 },
			{ "fld21", "fld21", ESIF_DATA_UINT64 },
			{ 0 }
		};
		fieldlist = appc_table_fields;
	}
	else if (esif_ccb_stricmp(self->name, "idsp") == 0) {
		static TableField idsp_fields [] = {
				{ "uuid", "uuid", ESIF_DATA_UINT64 },
				{ 0 }
		};
		fieldlist = idsp_fields;

	}
	else if (esif_ccb_stricmp(self->name, "ppcc") == 0) {
		static TableField ppcc_fields [] = {
				{ "PLIndex", "PLIndex", ESIF_DATA_UINT64 },
				{ "PLMin", "PLMin", ESIF_DATA_UINT64 },
				{ "PLMax", "PLMax", ESIF_DATA_UINT64 },
				{ "PLTimeMin", "PLTimeMin", ESIF_DATA_UINT64 },
				{ "PLTimeMax", "PLTimeMax", ESIF_DATA_UINT64 },
				{ "PLStep", "PLStep", ESIF_DATA_UINT64 },
				{ 0 }
		};
		fieldlist = ppcc_fields;
	}
	else if (esif_ccb_stricmp(self->name, "vsct") == 0) {
		static TableField vsct_fields [] = {
				{ "fld1", "fld1", ESIF_DATA_STRING },
				{ "fld2", "fld2", ESIF_DATA_UINT64 },
				{ "fld3", "fld3", ESIF_DATA_UINT64 },
				{ "fld4", "fld4", ESIF_DATA_UINT64 },
				{ "fld5", "fld5", ESIF_DATA_UINT64 },
				{ "fld6", "fld6", ESIF_DATA_UINT64 },
				{ "fld7", "fld7", ESIF_DATA_UINT64 },
				{ 0 }
		};
		fieldlist = vsct_fields;
	}
	else if (esif_ccb_stricmp(self->name, "vspt") == 0) {
		static TableField vspt_fields [] = {
				{ "fld1", "fld1", ESIF_DATA_UINT64 },
				{ "fld2", "fld2", ESIF_DATA_UINT64 },
				{ 0 }
		};
		fieldlist = vspt_fields;
	}
	else if (esif_ccb_stricmp(self->name, "ppss") == 0) {
		static TableField ppss_fields [] = {
				{ "Performance", "Performance", ESIF_DATA_UINT64 },
				{ "Power", "Power", ESIF_DATA_UINT64 },
				{ "TransitionLatency", "TransitionLatency", ESIF_DATA_UINT64 },
				{ "Linear", "Linear", ESIF_DATA_UINT64 },
				{ "Control", "Control", ESIF_DATA_UINT64 },
				{ "RawPerformance", "RawPerformance", ESIF_DATA_UINT64 },
				{ "RawUnit", "RawUnit", ESIF_DATA_STRING },
				{ "Reserved1", "Reserved1", ESIF_DATA_UINT64 },
				{ 0 }
		};
		fieldlist = ppss_fields;
	}
	else if (esif_ccb_stricmp(self->name, "pss") == 0) {
		static TableField pss_fields [] = {
				{ "ControlValue", "ControlValue", ESIF_DATA_UINT64 },
				{ "TDPPower", "TDPPower", ESIF_DATA_UINT64 },
				{ "Latency1", "Latency1", ESIF_DATA_UINT64 },
				{ "Latency2", "Latency2", ESIF_DATA_UINT64 },
				{ "ControlID1", "ControlID1", ESIF_DATA_UINT64 },
				{ "ControlID2", "ControlID2", ESIF_DATA_UINT64 },
				{ 0 }
		};
		fieldlist = pss_fields;
	}
	else if (esif_ccb_stricmp(self->name, "psv") == 0) {
		static TableField psv_fields [] = {
				{ "psv", "psv", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_PASSIVE, SET_TRIP_POINT_PASSIVE, ESIF_NO_INSTANCE },
				{ "cr3", "cr3", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_WARM, SET_TRIP_POINT_WARM, ESIF_NO_INSTANCE },
				{ "hot", "hot", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_HOT, SET_TRIP_POINT_HOT, ESIF_NO_INSTANCE },
				{ "crt", "crt", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_CRITICAL, SET_TRIP_POINT_CRITICAL, ESIF_NO_INSTANCE },
				{ "ac0", "ac0", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_ACTIVE, SET_TRIP_POINT_ACTIVE, 0 },
				{ "ac1", "ac1", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_ACTIVE, SET_TRIP_POINT_ACTIVE, 1 },
				{ "ac2", "ac2", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_ACTIVE, SET_TRIP_POINT_ACTIVE, 2 },
				{ "ac3", "ac3", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_ACTIVE, SET_TRIP_POINT_ACTIVE, 3 },
				{ "ac4", "ac4", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_ACTIVE, SET_TRIP_POINT_ACTIVE, 4 },
				{ "ac5", "ac5", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_ACTIVE, SET_TRIP_POINT_ACTIVE, 5 },
				{ "ac6", "ac6", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_ACTIVE, SET_TRIP_POINT_ACTIVE, 6 },
				{ "ac7", "ac7", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_ACTIVE, SET_TRIP_POINT_ACTIVE, 7 },
				{ "ac8", "ac8", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_ACTIVE, SET_TRIP_POINT_ACTIVE, 8 },
				{ "ac9", "ac9", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_ACTIVE, SET_TRIP_POINT_ACTIVE, 9 },
				{ "hyst", "hyst", ESIF_DATA_TEMPERATURE, GET_TEMPERATURE_THRESHOLD_HYSTERESIS, SET_TEMPERATURE_THRESHOLD_HYSTERESIS, ESIF_NO_INSTANCE },
				{ 0 }
		};
		fieldlist = psv_fields;
	}
	else if (esif_ccb_stricmp(self->name, "status") == 0) {
		static TableField status_fields [] = {
				{ "temp", "temp", ESIF_DATA_TEMPERATURE, GET_TEMPERATURE, SET_TEMPERATURE, ESIF_NO_INSTANCE },
				{ "power", "power", ESIF_DATA_POWER, GET_RAPL_POWER, 0, ESIF_NO_INSTANCE },
				{ "fanSpeed", "fanSpeed", ESIF_DATA_STRUCTURE, GET_FAN_STATUS, 0, ESIF_NO_INSTANCE },
				{ "battery", "battery", ESIF_DATA_STRUCTURE, GET_BATTERY_STATUS, 0, ESIF_NO_INSTANCE },
				{ "wrm", "wrm", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_WARM, SET_TRIP_POINT_WARM, ESIF_NO_INSTANCE },
				{ "hot", "hot", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_HOT, SET_TRIP_POINT_HOT, ESIF_NO_INSTANCE },
				{ "crt", "crt", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_CRITICAL, SET_TRIP_POINT_CRITICAL, ESIF_NO_INSTANCE },
				{ "psv", "psv", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_PASSIVE, SET_TRIP_POINT_PASSIVE, ESIF_NO_INSTANCE },
				{ "tempAux0", "tempAux0", ESIF_DATA_TEMPERATURE, GET_TEMPERATURE_THRESHOLDS, SET_TEMPERATURE_THRESHOLDS, 0 },
				{ "tempAux1", "tempAux1", ESIF_DATA_TEMPERATURE, GET_TEMPERATURE_THRESHOLDS, SET_TEMPERATURE_THRESHOLDS, 1 },
				{ "tempHyst", "tempHyst", ESIF_DATA_TEMPERATURE, GET_TEMPERATURE_THRESHOLD_HYSTERESIS, SET_TEMPERATURE_THRESHOLD_HYSTERESIS, ESIF_NO_INSTANCE },
				{ "ntt", "ntt", ESIF_DATA_TEMPERATURE, GET_NOTIFICATION_TEMP_THRESHOLD, 0, ESIF_NO_INSTANCE},
				{ "ac0", "ac0", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_ACTIVE, SET_TRIP_POINT_ACTIVE, 0 },
				{ "ac1", "ac1", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_ACTIVE, SET_TRIP_POINT_ACTIVE, 1 },
				{ "ac2", "ac2", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_ACTIVE, SET_TRIP_POINT_ACTIVE, 2 },
				{ "ac3", "ac3", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_ACTIVE, SET_TRIP_POINT_ACTIVE, 3 },
				{ "ac4", "ac4", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_ACTIVE, SET_TRIP_POINT_ACTIVE, 4 },
				{ "ac5", "ac5", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_ACTIVE, SET_TRIP_POINT_ACTIVE, 5 },
				{ "ac6", "ac6", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_ACTIVE, SET_TRIP_POINT_ACTIVE, 6 },
				{ "ac7", "ac7", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_ACTIVE, SET_TRIP_POINT_ACTIVE, 7 },
				{ "ac8", "ac8", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_ACTIVE, SET_TRIP_POINT_ACTIVE, 8 },
				{ "ac9", "ac9", ESIF_DATA_TEMPERATURE, GET_TRIP_POINT_ACTIVE, SET_TRIP_POINT_ACTIVE, 9 },
				{ "activeCoreCount", "activeCoreCount", ESIF_DATA_UINT32, GET_PROC_LOGICAL_PROCESSOR_COUNT, 0, ESIF_NO_INSTANCE },
				{ "pStateMax", "pStateMax", ESIF_DATA_UINT32, GET_PROC_PERF_PRESENT_CAPABILITY, SET_PERF_PRESENT_CAPABILITY, ESIF_NO_INSTANCE },
				{ "ppcc", "ppcc", ESIF_DATA_STRUCTURE, GET_RAPL_POWER_CONTROL_CAPABILITIES, 0, ESIF_NO_INSTANCE },
				{ "powerLimit1", "powerLimit1", ESIF_DATA_POWER, GET_RAPL_POWER_LIMIT, SET_RAPL_POWER_LIMIT, 0 },
				{ "powerTimeWindow1", "powerTimeWindow1", ESIF_DATA_TIME, GET_RAPL_POWER_LIMIT_TIME_WINDOW, SET_RAPL_POWER_LIMIT_TIME_WINDOW, 0 },
				{ "powerLimit2", "powerLimit2", ESIF_DATA_POWER, GET_RAPL_POWER_LIMIT, SET_RAPL_POWER_LIMIT, 1 },
				{ "powerLimit3", "powerLimit3", ESIF_DATA_POWER, GET_RAPL_POWER_LIMIT, SET_RAPL_POWER_LIMIT, 2 },
				{ "powerTimeWindow3", "powerTimeWindow3", ESIF_DATA_TIME, GET_RAPL_POWER_LIMIT_TIME_WINDOW, SET_RAPL_POWER_LIMIT_TIME_WINDOW, 2 },
				{ "powerDutyCycle3", "powerDutyCycle3", ESIF_DATA_UINT32, GET_RAPL_POWER_LIMIT_DUTY_CYCLE, SET_RAPL_POWER_LIMIT_DUTY_CYCLE, 2 },
				{ "powerLimit4", "powerLimit4", ESIF_DATA_POWER, GET_RAPL_POWER_LIMIT, SET_RAPL_POWER_LIMIT, 3 },
				{ "platformPowerLimit1", "platformPowerLimit1", ESIF_DATA_POWER, GET_PLATFORM_POWER_LIMIT, SET_PLATFORM_POWER_LIMIT, 0 },
				{ "platformPowerTimeWindow1", "platformPowerTimeWindow1", ESIF_DATA_TIME, GET_PLATFORM_POWER_LIMIT_TIME_WINDOW, SET_PLATFORM_POWER_LIMIT_TIME_WINDOW, 0 },
				{ "platformPowerLimit2", "platformPowerLimit2", ESIF_DATA_POWER, GET_PLATFORM_POWER_LIMIT, SET_PLATFORM_POWER_LIMIT, 1 },
				{ "platformPowerLimit3", "platformPowerLimit3", ESIF_DATA_POWER, GET_PLATFORM_POWER_LIMIT, SET_PLATFORM_POWER_LIMIT, 2 },
				{ "platformPowerTimeWindow3", "platformPowerTimeWindow3", ESIF_DATA_TIME, GET_PLATFORM_POWER_LIMIT_TIME_WINDOW, SET_PLATFORM_POWER_LIMIT_TIME_WINDOW, 2 },
				{ "platformPowerDutyCycle3", "platformPowerDutyCycle3", ESIF_DATA_UINT32, GET_PLATFORM_POWER_LIMIT_DUTY_CYCLE, SET_PLATFORM_POWER_LIMIT_DUTY_CYCLE, 2 },
				{ "platformPowerLimit4", "platformPowerLimit4", ESIF_DATA_POWER, GET_PLATFORM_POWER_LIMIT, SET_PLATFORM_POWER_LIMIT, 3 },
				{ "samplePeriod", "samplePeriod", ESIF_DATA_UINT32, GET_PARTICIPANT_SAMPLE_PERIOD, SET_PARTICIPANT_SAMPLE_PERIOD, ESIF_NO_INSTANCE },
				{ "pmax", "pmax", ESIF_DATA_POWER, GET_PLATFORM_MAX_BATTERY_POWER, 0, ESIF_NO_INSTANCE },
				{ "pbss", "pbss", ESIF_DATA_POWER, GET_PLATFORM_BATTERY_STEADY_STATE, 0, ESIF_NO_INSTANCE },
				{ "prop", "prop", ESIF_DATA_POWER, GET_PLATFORM_REST_OF_POWER, 0, ESIF_NO_INSTANCE },
				{ "artg", "artg", ESIF_DATA_POWER, GET_ADAPTER_POWER_RATING, 0, ESIF_NO_INSTANCE },
				{ "ctyp", "ctyp", ESIF_DATA_UINT32, GET_CHARGER_TYPE, 0, ESIF_NO_INSTANCE },
				{ "psrc", "psrc", ESIF_DATA_UINT32, GET_PLATFORM_POWER_SOURCE, 0, ESIF_NO_INSTANCE },
				{ "avol", "avol", ESIF_DATA_UINT32, GET_AVOL, 0, ESIF_NO_INSTANCE },
				{ "acur", "acur", ESIF_DATA_UINT32, GET_ACUR, 0, ESIF_NO_INSTANCE },
				{ "ap01", "ap01", ESIF_DATA_UINT32, GET_AP01, 0, ESIF_NO_INSTANCE },
				{ "ap02", "ap02", ESIF_DATA_UINT32, GET_AP02, 0, ESIF_NO_INSTANCE },
				{ "ap10", "ap10", ESIF_DATA_UINT32, GET_AP10, 0, ESIF_NO_INSTANCE },
				{ "odvp", "odvp", ESIF_DATA_STRUCTURE, GET_OEM_VARS, 0, ESIF_NO_INSTANCE },
				{ 0 }
		};
		fieldlist = status_fields;
	}
	else if (esif_ccb_stricmp(self->name, "workload") == 0) {
		static TableField workload_fields [] = {
				{ "workload", "workload", ESIF_DATA_STRING, 0, 0, ESIF_NO_INSTANCE, "/shared/export/workload_hints" },
				{ 0 }
		};
		fieldlist = workload_fields;
	}
	else if (esif_ccb_stricmp(self->name, "participant_min") == 0) {
		static TableField pmin_fields [] = {
				{ "temp", "temp", ESIF_DATA_TEMPERATURE, GET_TEMPERATURE, SET_TEMPERATURE, ESIF_NO_INSTANCE },
				{ "power", "power", ESIF_DATA_POWER, GET_RAPL_POWER, 0, ESIF_NO_INSTANCE },
				{ 0 }
		};
		fieldlist = pmin_fields;
	}
	else if (esif_ccb_stricmp(self->name, "pdl") == 0) {
		static TableField pdl_fields [] = {
				{ "_pdl", "_pdl", ESIF_DATA_UINT32, GET_PROC_PERF_PSTATE_DEPTH_LIMIT, SET_PROC_PERF_PSTATE_DEPTH_LIMIT, ESIF_NO_INSTANCE },
				{ 0 }
		};
		fieldlist = pdl_fields;
	}
	else if (esif_ccb_stricmp(self->name, "ppdl") == 0) {
		static TableField ppdl_fields [] = {
				{ "ppdl", "ppdl", ESIF_DATA_UINT32, GET_PERF_PSTATE_DEPTH_LIMIT, SET_PERF_PSTATE_DEPTH_LIMIT, ESIF_NO_INSTANCE },
				{ 0 }
		};
		fieldlist = ppdl_fields;
	}
	else if (esif_ccb_stricmp(self->name, "ecmt") == 0) {
		static TableField ecmt_fields[] = {
			{ "fld1", "fld1", ESIF_DATA_STRING },
			{ "fld2", "fld2", ESIF_DATA_UINT64 },
			{ "fld3", "fld3", ESIF_DATA_UINT64 },
			{ "fld4", "fld4", ESIF_DATA_UINT64 },
			{ "fld5", "fld5", ESIF_DATA_UINT64 },
			{ 0 }
		};
		fieldlist = ecmt_fields;
	}
	else if (esif_ccb_stricmp(self->name, "pida") == 0 && self->version == 1) {
		static TableField pida_fields[] = {
			{ "fld1", "fld1", ESIF_DATA_STRING },
			{ "fld2", "fld2", ESIF_DATA_UINT64 },
			{ "fld3", "fld3", ESIF_DATA_UINT64 },
			{ "fld4", "fld4", ESIF_DATA_STRING },
			{ "fld5", "fld5", ESIF_DATA_UINT64 },
			{ "fld6", "fld6", ESIF_DATA_UINT64 },
			{ "fld7", "fld7", ESIF_DATA_UINT64 },
			{ "fld8", "fld8", ESIF_DATA_UINT64 },
			{ "fld9", "fld9", ESIF_DATA_UINT64 },
			{ "fld10", "fld10", ESIF_DATA_UINT64 },
			{ "fld11", "fld11", ESIF_DATA_UINT64 },
			{ 0 }
		};
		fieldlist = pida_fields;
	}
	else if (esif_ccb_stricmp(self->name, "pida") == 0 && self->version == 2) {
		static TableField pida_fields[] = {
			{ "fld1", "fld1", ESIF_DATA_STRING },
			{ "fld2", "fld2", ESIF_DATA_UINT64 },
			{ "fld3", "fld3", ESIF_DATA_UINT64 },
			{ "fld4", "fld4", ESIF_DATA_STRING },
			{ "fld5", "fld5", ESIF_DATA_UINT64 },
			{ "fld6", "fld6", ESIF_DATA_UINT64 },
			{ "fld7", "fld7", ESIF_DATA_UINT64 },
			{ "fld8", "fld8", ESIF_DATA_UINT64 },
			{ "fld9", "fld9", ESIF_DATA_UINT64 },
			{ "fld10", "fld10", ESIF_DATA_UINT64 },
			{ "fld11", "fld11", ESIF_DATA_UINT64 },
			{ "fld12", "fld12", ESIF_DATA_UINT64 },
			{ 0 }
		};
		fieldlist = pida_fields;
	}
	else if (esif_ccb_stricmp(self->name, "acpr") == 0) {
		static TableField acpr_fields[] = {
			{ "fld1", "fld1", ESIF_DATA_STRING },
			{ "fld2", "fld2", ESIF_DATA_UINT64 },
			{ "fld3", "fld3", ESIF_DATA_STRING },
			{ "fld4", "fld4", ESIF_DATA_UINT64 },
			{ "fld5", "fld5", ESIF_DATA_UINT64 },
			{ "fld6", "fld6", ESIF_DATA_UINT64 },
			{ "fld7", "fld7", ESIF_DATA_UINT64 },
			{ 0 }
		};
		fieldlist = acpr_fields;
	}
	else if (esif_ccb_stricmp(self->name, "psha") == 0) {
		static TableField psha_fields[] = {
			{ "fld1", "fld1", ESIF_DATA_STRING },
			{ "fld2", "fld2", ESIF_DATA_UINT64 },
			{ "fld3", "fld3", ESIF_DATA_UINT64 },
			{ 0 }
		};
		fieldlist = psha_fields;
	}
	else {
		rc = ESIF_E_NOT_IMPLEMENTED;
	}

	/* Construct Field List and DataVault Key */
	if (fieldlist != NULL) {
		int j = 0;
		int k = 0;
		while (fieldlist[j].name != NULL) {
			++j;
		}

		if (j <= 0) {
			goto exit;
		}

		self->numFields = j;
		self->fields = (TableField *)esif_ccb_malloc(sizeof(TableField) * (size_t)j);
		esif_ccb_memset(self->fields, 0, sizeof(TableField) * (size_t)j);

		if (self->dataType == ESIF_DATA_BINARY) {
			for (k = 0; k < self->numFields; k++) {
				TableField_Construct(&(self->fields[k]), fieldlist[k].name, fieldlist[k].label, fieldlist[k].dataType, fieldlist[k].notForXML);
			}
		}
		else {   /* virtual tables */
			for (k = 0; k < self->numFields; k++) {
				TableField_ConstructAs(&(self->fields[k]), fieldlist[k].name, fieldlist[k].label, fieldlist[k].dataType, fieldlist[k].notForXML, fieldlist[k].getPrimitive, fieldlist[k].setPrimitive, fieldlist[k].instance, fieldlist[k].dataVaultKey);
			}
		}

		if (self->dataMember == NULL) {
			esif_ccb_sprintf(MAX_TABLEOBJECT_KEY_LEN, targetKey,
				"/participants/%s.%s/%.*s%s",
				EsifUp_GetName(upPtr), self->domainQualifier,
				(int) (ACPI_NAME_TARGET_SIZE - esif_ccb_min(esif_ccb_strlen(self->name, ACPI_NAME_TARGET_SIZE), ACPI_NAME_TARGET_SIZE)), "____",
				self->name);
			self->dataMember = esif_ccb_strdup(targetKey);
		}
	}

exit:
	if (upPtr != NULL) {
		EsifUp_PutRef(upPtr);
	}
	return rc;
}

void TableObject_Destroy(TableObject *self)
{
	int i;
	if (self != NULL) {
		for (i = 0; i < self->numFields; i++) {
			TableField_Destroy(&(self->fields[i]));
		}
		esif_ccb_free(self->fields);
		esif_ccb_free(self->name);
		esif_ccb_free(self->domainQualifier);
		esif_ccb_free(self->dataText);
		esif_ccb_free(self->binaryData);
		esif_ccb_free(self->dataXML);
		esif_ccb_free(self->dataVaultCategory);
		esif_ccb_free(self->dataSource);
		esif_ccb_free(self->dataMember);
		esif_ccb_memset(self, 0, sizeof(*self));
	}
}
