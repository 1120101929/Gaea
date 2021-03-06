/*
 *  Copyright Beijing 58 Information Technology Co.,Ltd.
 *
 *  Licensed to the Apache Software Foundation (ASF) under one
 *  or more contributor license agreements.  See the NOTICE file
 *  distributed with this work for additional information
 *  regarding copyright ownership.  The ASF licenses this file
 *  to you under the Apache License, Version 2.0 (the
 *  "License"); you may not use this file except in compliance
 *  with the License.  You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing,
 *  software distributed under the License is distributed on an
 *  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *  KIND, either express or implied.  See the License for the
 *  specific language governing permissions and limitations
 *  under the License.
 */
/*
 * FileScan.cpp
 *
 *  Created on: 2011-7-20
 *  Author: Service Platform Architecture Team (spat@58.com)
 */
#include "input.h"
#include "../serialize/serializer.h"
#include "../serialize/serializeList.h"
#include "../serialize/strHelper.h"
#include "../client/GaeaConst.h"
#include <objc/hash.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>
#include <assert.h>
#include <errno.h>
#include <time.h>
#include <stddef.h>
#include <string>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <map>
#include <vector>
#define IsLetter(c)        ((c >= 'a' && c <= 'z') || (c == '_') || (c >= 'A' && c <= 'Z'))
#define IsDigit(c)         (c >= '0' && c <= '9')
#define IsLetterOrDigit(c) (IsLetter(c) || IsDigit(c))
#define max(i,j)(i>j?i:j)
struct coord TokenCoord;
unsigned char END_OF_FILE = 255;
typedef struct {
	std::string name;
	std::string type;
	int offset;
	int hashcode;
	char isPointe;
} scanFieldInfo;
typedef struct {
	std::string name;
	int size;
	int fieldMaxSize;
	char isPointe;
	std::vector<scanFieldInfo*> fields;
} scanStructInfo;
std::map<std::string, scanStructInfo*> structMap;
std::map<std::string, int> typeSizeInfo;
void nextLine(void) {
	while (*CURSOR != '\n' && *CURSOR != END_OF_FILE) {
		CURSOR++;
	}
	if (*CURSOR == '\n') {
		CURSOR++;
	}
}
static void SkipWhiteSpace(void) {
	int ch;

	ch = *CURSOR;
	while (ch == '\t' || ch == '\v' || ch == '\f' || ch == ' ' || ch == '\r' || ch == '\n' || ch == '/' || ch == '#') {
		switch (ch) {
		case '\n':
			TokenCoord.ppline++;
			LINE++;
			LINEHEAD = ++CURSOR;
			break;

		case '#':
			while (*CURSOR != '\n' && *CURSOR != END_OF_FILE) {
				CURSOR++;
			}

			break;

		case '/':
			if (CURSOR[1] != '/' && CURSOR[1] != '*')
				return;
			CURSOR++;
			if (*CURSOR == '/') {
				CURSOR++;
				while (*CURSOR != '\n' && *CURSOR != END_OF_FILE) {
					CURSOR++;
				}
			} else {
				CURSOR += 2;
				while (CURSOR[0] != '*' || CURSOR[1] != '/') {
					if (*CURSOR == '\n') {
						TokenCoord.ppline++;
						LINE++;
					} else if (CURSOR[0] == END_OF_FILE || CURSOR[1] == END_OF_FILE) {
						return;
					}
					CURSOR++;
				}
				CURSOR += 2;
			}
			break;

		default:
			CURSOR++;
			break;
		}
		ch = *CURSOR;
	}

}
int findKeyWord(unsigned char **start) {
	SkipWhiteSpace();
	*start = CURSOR;
	while (IsLetter(*CURSOR)) {
		++CURSOR;
	}
	std::string typeName = std::string((char*) *start, CURSOR - *start);
	std::map<std::string, int>::iterator it = typeSizeInfo.find(typeName);
	if (it != typeSizeInfo.end()) {
		if (it->second > 0) {
			return 1;
		} else {
			return findKeyWord(start);
		}
	}
	return 0;
}
int getOffset(int offset, int size) {
	if (offset % size == 0) {
		return offset;
	}
	return (offset / size + 1) * size;
}

void analyseStruct(scanStructInfo *ssi) {
	++CURSOR;
	int offset = 0;
	int maxSize = 0;
	char isPointe = 0;
	char isStruct = 0;
	unsigned char *start;
	int size;
	std::string typeName;
	std::string variableName;
	scanFieldInfo *sfi;
	bool voidFlg = false;
	bool typeIdFlg = false;
	while (*CURSOR != '}' && *CURSOR != END_OF_FILE) {
		typeIdFlg = false;
		isPointe = 0;
		typeName = "";
		variableName = "";
		isStruct = 0;
		while (findKeyWord(&start)) {
			if (typeName == "") {
				typeName = std::string((char*) start, CURSOR - start);
			} else {
				typeName += " " + std::string((char*) start, CURSOR - start);
			}
		}
		isStruct = typeSizeInfo.find(typeName) == typeSizeInfo.end();
		if (typeName == "") {
			typeName = std::string((char*) start, CURSOR - start);
		} else {
			variableName = std::string((char*) start, CURSOR - start);
		}
		if (voidFlg) {
			if (typeName == "int") {
				typeIdFlg = true;
				voidFlg = false;
			} else {
				throw std::runtime_error("数据结构错误，void*指针字段后面应该是int类型，用来标示void*类型。");
			}
		}
		if (typeName == "void") {
			voidFlg = true;
		}
		if (variableName.size() > 0) {
			sfi = new scanFieldInfo;
			sfi->isPointe = isPointe;
			if (sfi->isPointe) {
				maxSize = size = sizeof(void*);
			} else if (isStruct) {
				std::map<std::string, scanStructInfo*>::iterator tempSSI = structMap.find(typeName);
				if (tempSSI == structMap.end()) {
					errno = -3;
					throw std::runtime_error("not  found struct name " + typeName);
				}
				maxSize = tempSSI->second->fieldMaxSize;
				size = tempSSI->second->size;
			} else {
				maxSize = size = typeSizeInfo.find(typeName)->second;
			}

			sfi->offset = getOffset(offset, maxSize);
			offset = sfi->offset;
			sfi->name = variableName;
			for (size_t i = 0; i < variableName.size(); ++i) {
				if (variableName[i] >= 'A' && variableName[i] <= 'Z') {
					variableName[i] += 32;
				}
			}
			sfi->hashcode = GetHashcode(variableName.c_str(), variableName.size());
			sfi->type = typeName;
			offset += size;
			if (!typeIdFlg) {
				ssi->fields.push_back(sfi);
			}
			ssi->fieldMaxSize = max(ssi->fieldMaxSize,maxSize);
		}
		SkipWhiteSpace();
		while (*CURSOR != ';' && *CURSOR != END_OF_FILE) {
			isPointe = 0;
			SkipWhiteSpace();
			if (*CURSOR == ',') {
				continue;
			} else if (*CURSOR == ';') {
				break;
			}
			sfi = new scanFieldInfo;
			if (*CURSOR == '*') {
				isPointe = 1;
				++CURSOR;
				SkipWhiteSpace();
			}
			unsigned char *start = CURSOR;
			while (IsLetterOrDigit(*CURSOR)) {
				++CURSOR;
			}
			variableName = std::string((char*) start, CURSOR - start);
			if (isPointe) {
				maxSize = size = sizeof(void*);
			} else if (isStruct) {
				std::map<std::string, scanStructInfo*>::iterator tempSSI = structMap.find(typeName);
				if (tempSSI == structMap.end()) {
					errno = -3;
					throw std::runtime_error("not  found struct name " + typeName);
				}
				if (tempSSI->second->isPointe) {
					isPointe = tempSSI->second->isPointe;
				}
				maxSize = tempSSI->second->fieldMaxSize;
				size = tempSSI->second->size;
			} else {
				maxSize = size = typeSizeInfo.find(typeName)->second;
			}
			sfi->offset = getOffset(offset, maxSize);
			offset = sfi->offset;
			sfi->isPointe = isPointe;
			sfi->name = variableName;
			for (size_t i = 0; i < variableName.size(); ++i) {
				if (variableName[i] >= 'A' && variableName[i] <= 'Z') {
					variableName[i] += 32;
				}
			}
			sfi->hashcode = GetHashcode(variableName.data(), variableName.size());
			sfi->type = typeName;
			offset += size;
			if (!typeIdFlg) {
				ssi->fields.push_back(sfi);
			}
			ssi->fieldMaxSize = max(ssi->fieldMaxSize,maxSize);
		}
		nextLine();
		SkipWhiteSpace();
	}
	ssi->size = getOffset(offset, ssi->fieldMaxSize);
}
void getStructName(char structname[][50], int *structNameLen) {
	int index = 0;
	unsigned char * start = CURSOR;
	char flg = 0;
	if (*CURSOR == '*') {
		structname[*structNameLen][0] = '*';
		++CURSOR;
		SkipWhiteSpace();
		index = 1;
		start = CURSOR;
	}
	while (IsLetterOrDigit(*CURSOR)) {
		if (*CURSOR == '_') {
			flg = 1;
		}
		++CURSOR;
	}
	memcpy(structname[*structNameLen] + index, start, CURSOR - start);
	structname[*structNameLen][index + CURSOR - start] = '\0';
	(*structNameLen)++;
	if (flg == 1) {
		for (int i = 0; i < index + CURSOR - start; ++i) {
			if (structname[*structNameLen - 1][i] != '_') {
				structname[*structNameLen][i] = structname[*structNameLen - 1][i];
			} else {
				structname[*structNameLen][i] = '.';
			}
		}
		structname[*structNameLen][index + CURSOR - start] = '\0';
		(*structNameLen)++;
	}
	SkipWhiteSpace();
	if (*CURSOR == ',') {
		++CURSOR;
		SkipWhiteSpace();
		getStructName(structname, structNameLen);
	}
}

void structScan(std::string &fileName) {
	ReadSourceFile((char*) fileName.c_str());
	char typedefFlg;
	char structAliasName[10][50];
	int structNameLen = 0;
	scanStructInfo *ssi, *ssi1;
	while (*CURSOR != END_OF_FILE) {
		structNameLen = 0;
		switch (*CURSOR) {
		case 's':
			if (strncmp((char*) CURSOR, "struct", 6) == 0) {
				CURSOR += 6;
				unsigned char *start = CURSOR;
				SkipWhiteSpace();
				if (start == CURSOR && *CURSOR != '{') {
					goto labelBack;
				}
				if (IsLetterOrDigit(*CURSOR)) {
					getStructName(structAliasName, &structNameLen);
					SkipWhiteSpace();
				}
				if (*CURSOR == '{') {
					ssi = new scanStructInfo;
					analyseStruct(ssi);
				} else {
					goto labelBack;
				}
				if (*CURSOR != '}') {
					errno = -3;
					throw std::runtime_error("analyse struct failed");
				} else {
					++CURSOR;
				}
				if (typedefFlg || structNameLen == 0) {
					SkipWhiteSpace();
					if (IsLetterOrDigit(*CURSOR) || *CURSOR == '*') {
						getStructName(structAliasName, &structNameLen);
						SkipWhiteSpace();
					}
				}

				if (structNameLen > 0) {

					std::string name;
					if (structAliasName[0][0] == '*') {
						name = std::string(structAliasName[0] + 1);
						ssi->isPointe = 1;
					} else {
						name = std::string(structAliasName[0]);
						ssi->isPointe = 0;
					}
					ssi->name = name;
					structMap.insert(std::pair<std::string, scanStructInfo*>(name, ssi));
					for (int i = 1; i < structNameLen; ++i) {
						ssi1 = new scanStructInfo;
						ssi1->fieldMaxSize = ssi->fieldMaxSize;
						ssi1->fields = ssi->fields;
						ssi1->size = ssi->size;
						if (structAliasName[i][0] == '*') {
							name = std::string(structAliasName[i] + 1);
							ssi1->isPointe = 1;
						} else {
							name = std::string(structAliasName[i]);
							ssi1->isPointe = 0;
						}
						ssi1->name = name;
						structMap.insert(std::pair<std::string, scanStructInfo*>(name, ssi1));
					}
				}
			}
			labelBack: nextLine();
			typedefFlg = 0;
			break;
		case 't':
			if (strncmp((char*) CURSOR, "typedef", 7) == 0 && (CURSOR[7] == ' ' || CURSOR[7] == '\t')) {
				CURSOR += 8;
				typedefFlg = 1;
				SkipWhiteSpace();
			} else {
				nextLine();
				typedefFlg = 0;
			}
			break;
		default:
			typedefFlg = 0;
			nextLine();
			break;
		}
		SkipWhiteSpace();
	}
}
void fileScan(std::string &dn) {
	DIR * pdir = opendir(dn.c_str());
	assert(pdir);

	struct dirent * pent;
	while ((pent = readdir(pdir))) {
		std::string pfn(pent->d_name);
		if (dn.at(dn.length() - 1) == '/') {
			pfn = dn + pfn;
		} else {
			pfn = dn + "/" + pfn;
		}
		if (pent->d_type & 8) {
			std::cout << pfn.c_str() << std::endl;
			structScan(pfn);
		} else if (strcmp(pent->d_name, "..") != 0 && strcmp(pent->d_name, ".") != 0) {
			std::cout << pfn.c_str() << std::endl;
			fileScan(pfn);
		}
	}
	closedir(pdir);
}
void setTypeSizeInfo() {
	typeSizeInfo.insert(std::pair<std::string, int>("char", sizeof(char)));
	typeSizeInfo.insert(std::pair<std::string, int>("short", sizeof(short)));
	typeSizeInfo.insert(std::pair<std::string, int>("int", sizeof(int)));
	typeSizeInfo.insert(std::pair<std::string, int>("long", sizeof(long)));
	typeSizeInfo.insert(std::pair<std::string, int>("long long", sizeof(long long)));
	typeSizeInfo.insert(std::pair<std::string, int>("float", sizeof(float)));
	typeSizeInfo.insert(std::pair<std::string, int>("double", sizeof(double)));
	typeSizeInfo.insert(std::pair<std::string, int>("size_t", sizeof(size_t)));
	typeSizeInfo.insert(std::pair<std::string, int>("time_t", sizeof(time_t)));
	typeSizeInfo.insert(std::pair<std::string, int>("array", sizeof(array)));
	typeSizeInfo.insert(std::pair<std::string, int>("cache_ptr", sizeof(cache_ptr)));
	typeSizeInfo.insert(std::pair<std::string, int>("serialize_list", sizeof(serialize_list)));
	typeSizeInfo.insert(std::pair<std::string, int>("enum_field", sizeof(enum_field)));
	typeSizeInfo.insert(std::pair<std::string, int>("void", sizeof(void*)));
	typeSizeInfo.insert(std::pair<std::string, int>("bool", 1));
	typeSizeInfo.insert(std::pair<std::string, int>("BOOL", 1));
	typeSizeInfo.insert(std::pair<std::string, int>("BOOLEAN", 1));
	typeSizeInfo.insert(std::pair<std::string, int>("struct", 0));
	typeSizeInfo.insert(std::pair<std::string, int>("unsigned", 0));
	typeSizeInfo.insert(std::pair<std::string, int>("signed", 0));
}
void writeConfig() {
	FILE *fp = fopen(STURCT_CONFIG_PATH, "w");
	std::map<std::string, scanStructInfo*>::iterator tempSSI = structMap.begin();
	scanStructInfo *ssi;
	scanFieldInfo *sfi, *sfi1;
	while (tempSSI != structMap.end()) {
		ssi = tempSSI->second;
		fprintf(fp, "%s,%d,%d,%d;", ssi->name.c_str(), GetHashcode(ssi->name.c_str(), ssi->name.size()), ssi->size, ssi->isPointe);
		for (size_t i = 0; i < ssi->fields.size(); ++i) {
			sfi = ssi->fields.operator [](i);
			for (size_t j = i + 1; j < ssi->fields.size(); ++j) {
				sfi1 = ssi->fields.operator [](j);
				if (sfi->hashcode > sfi1->hashcode) {
					ssi->fields.operator [](i) = sfi1;
					ssi->fields.operator [](j) = sfi;
					sfi = sfi1;
				}
			}
		}
		for (size_t i = 0; i < ssi->fields.size(); ++i) {
			sfi = ssi->fields.operator [](i);
			fprintf(fp, "%s,%s,%d,%d,%d;", sfi->name.c_str(), sfi->type.c_str(), sfi->hashcode, sfi->offset, sfi->isPointe);
		}
		tempSSI++;
		fprintf(fp, "\n");
	}
	fflush(fp);
	fclose(fp);
}
int mainY(int argc, char *argv[]) {
	setTypeSizeInfo();
	if (argc == 1) {
		printf("Please enter the scan directory");
		exit(1);
	}
	if (argv[1][0] != '-') {
		exit(1);
	}
	char f = 0;
	if (argv[1][1] == 'f') {
		f = 0;
	} else {
		f = 1;
	}
	for (int i = 2; i < argc; ++i) {
		std::string s(argv[i]);
		if (f) {
			fileScan(s);
		} else {
			structScan(s);
		}
	}
	writeConfig();
	return 0;
}
