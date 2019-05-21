/*
 * Copyright (c) 2019-present Samsung Electronics Co., Ltd
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
 *  USA
 */

#include "Escargot.h"
#include "runtime/VMInstance.h"
#include "runtime/ExecutionContext.h"
#include "util/Vector.h"
#include "runtime/Value.h"
#include "parser/ScriptParser.h"
#include "interpreter/ByteCodeGenerator.h"
#include "snapshot/Snapshot.h"

char* readFile(const char* filename, const char* mode) {
    FILE *file;
    char *buffer;
    unsigned long fileLen = 0;

    file = fopen(filename, mode);
    if (!file) {
        fprintf(stderr, "Cannot open file.\n");
        exit(23);
    }

    //Get file length
    fseek(file, 0, SEEK_END);
    fileLen = ftell(file);
    fseek(file, 0, SEEK_SET);

    //Allocate memory
    buffer = (char*)malloc(fileLen + 1);
    if (!buffer) {
        fprintf(stderr, "Cannot allocate memory.\n");
        exit(23);
    }

    //Read file contents into buffer
    fread(buffer, fileLen, 1, file);
    fclose(file);

    return buffer;
}

void generate(Escargot::Context* context, const char* filename, const char* source) {
    Escargot::String* fname =  String::fromASCII(filename);
    Escargot::String* src = String::fromASCII(source);

    Escargot::Snapshot::generate(context, fname, src);
}

void execute(Escargot::Context* context, const char* source) {
    Escargot::Snapshot::execute(context, source);
}

int main(int argc, char* argv[])
{
    Escargot::Heap::initialize();
    Escargot::VMInstance* instance = new Escargot::VMInstance();
    Escargot::Context* context = new Escargot::Context(instance);

    if (argc < 3) {
        printf("Usage: %s <--generate | --execute><filename>\n", argv[0]);
        exit(1);
    }

    char* option = argv[1];
    char* filename = argv[2];
    char* src = readFile(filename, "r");

    if (strcmp(option, "--generate") == 0) {
        generate(context, filename, src);
    } else if (strcmp(option, "--execute") == 0) {
        execute(context, src);
    }

    free(src);
    delete context;
    delete instance;

    Escargot::Heap::finalize();

    return 0;
}
