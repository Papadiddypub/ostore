/******************************************************************************
* OStore - a simple resliant binary object storage format                     *
*                                                                             *
* Copyright Notice and License                                                *
*  (c) Copyright 2020 Chris Woods.                                            *
*                                                                             *
*  Licensed under the Apache License, Version 2.0 (the "License"); you may    *
*  not use this file except in compliance with the License. You may obtain a  *
*  copy of the License at :  [http://www.apache.org/licenses/LICENSE-2.0]     *
*                                                                             *
*  Unless required by applicable law or agreed to in writing, software        *
*  distributed under the License is distributed on an "AS IS" BASIS,          *
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   *
*  See the License for the specific language governing permissions and        *
*  limitations under the License.                                             *
*                                                                             *
******************************************************************************/



#include "ostore.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <assert.h>

#include "ostore_internal.h"

#include "ostoreconfig.h"

#ifndef CMAKE_BUILD_ON
#define ostore_VERSION_MAJOR 1
#define ostore_VERSION_MINOR 2
#endif // CMAKE_BUILD_ON

// Store Handling
int ostore_open(const char* filename, TOStreamMode mode, TOStoreHnd* oStore) {
    assert(oStore != NULL);
    assert(filename != NULL);
    assert(mode == EReadOnly || mode == EReadWrite);
    START;
    LOCAL_MEMZ(TDskObjectStoreBlockHeader, firstBlockHeader);
    uint8_t* bytePtr = NULL;
    const char* fileMode = NULL;
    
    TOStore* store = (TOStore*) zmalloc(TOStore);
    if ( store == NULL )
        HANDLE_ERROR(ERR_MEM);

    
    switch(mode) {
        case EReadWrite:
        fileMode = "r+"; // drop the 'b' binary
        break;

        case EReadOnly:
        fileMode = "r";
        break;        
    }
    store->fileMode = mode;

    store->fp = fopen(filename, fileMode);
    VALIDATE(store->fp == NULL, ERR_NOT_FOUND);

    bytePtr = (uint8_t*)&store->fileHeader.header;
    retval = readFromFile(store->fp, 0, sizeof(TDskObjectStoreFileHeader), bytePtr);
    IF_NOT_OK_HANDLE_ERROR(retval);
    VALIDATE( !IDS_MATCH(FILE_ID, store->fileHeader.header.identifyingWord), ERR_CORRUPT);
    VALIDATE(store->fileHeader.header.versionNumner != VERSION, ERR_CORRUPT);


    
    bytePtr = (uint8_t*)&firstBlockHeader;
    retval = readFromFile(store->fp, FILE_LOCATION_FOR_FIRST_BLOCK, sizeof(TDskObjectStoreBlockHeader), bytePtr);
    IF_NOT_OK_HANDLE_ERROR(retval);
    VALIDATE( !IDS_MATCH(BLOCK_ID, firstBlockHeader.identifyingWord), ERR_CORRUPT);
    VALIDATE( firstBlockHeader.id != OBJECT_TABLE_ID, ERR_CORRUPT);
    VALIDATE( firstBlockHeader.last != NO_BLOCK, ERR_CORRUPT);
    VALIDATE( firstBlockHeader.sequenceNumber != 0, ERR_CORRUPT);

    // boot strapping here
    bytePtr = (uint8_t*)&store->numberOfObjects;
    retval = readFromFile(store->fp, FILE_LOCATION_FOR_NUMBER_OF_OBJECTS, sizeof(uint32_t), bytePtr);
    IF_NOT_OK_HANDLE_ERROR(retval);

    bytePtr = (uint8_t*)&store->tableOfObjectsHeader.header;
    retval = readFromFile(store->fp, FILE_LOCATION_FOR_TABLE_OF_OBJECTS_INDEX, sizeof(TDskObjIndex), bytePtr);
    IF_NOT_OK_HANDLE_ERROR(retval);

    bytePtr = (uint8_t*)&store->tashHeader.header;
    retval = readFromFile(store->fp, FILE_LOCATION_FOR_TRASH_INDEX, sizeof(TDskObjIndex), bytePtr);
    IF_NOT_OK_HANDLE_ERROR(retval);


    (*oStore) = store;

    PROCESS_ERROR;
    printf("error (%d) ocurred opening %s with mode %d\n", retval, filename, mode);
    if ( store && store->fp) {
        fclose(store->fp);
    }
    zfree(store);

   FINISH;
}

int ostore_create(const char* filename, TOStoreHnd* oStore) {
    assert(oStore != NULL);
    assert(filename != NULL);
    START;

    uint8_t* dataPtr = NULL;
    LOCAL_MEMZ(TDskObjectStoreBlockHeader, firstBlockHeader);

    TOStore* store = (TOStore*) zmalloc(TOStore);
    if ( store == NULL )
        HANDLE_ERROR(ERR_MEM);

    store->fileHeader.header.blocksInFile = 1;
    store->fileHeader.header.blockSize = DEFUALT_BLOCKSIZE;
    SET_ID(store->fileHeader.header.identifyingWord, FILE_ID);
    store->fileHeader.header.versionNumner = VERSION;
    store->numberOfObjects = INITIAL_NUMBER_OF_OBJECTS;

    store->tableOfObjectsHeader.header.headBlock = 0;
    store->tableOfObjectsHeader.header.tailBlock = 0;
    store->tableOfObjectsHeader.header.id = OBJECT_TABLE_ID;
    store->tableOfObjectsHeader.header.numberOfBlocks = 1;

    store->tashHeader.header.headBlock = NO_BLOCK;
    store->tashHeader.header.tailBlock = NO_BLOCK;
    store->tashHeader.header.id = TRASH_TABLE_ID;
    store->tashHeader.header.numberOfBlocks = 0;

    firstBlockHeader.blockFileIndex = 0;
    firstBlockHeader.id = OBJECT_TABLE_ID;
    SET_ID(firstBlockHeader.identifyingWord, BLOCK_ID);
    firstBlockHeader.last = NO_BLOCK;
    firstBlockHeader.next = NO_BLOCK;
    firstBlockHeader.sequenceNumber = 0;


    store->fp = fopen(filename, "w+"); // was bw+
    VALIDATE(store->fp == NULL, ERR_CORRUPT);

    //uint8_t* dataPtr = (uint8_t*)&store->fileHeader.header;
    //retval = writeToFile(store->fp, 0, sizeof(TDskObjectStoreFileHeader), dataPtr);
    
    retval = updateFileHeader(store);
    IF_NOT_OK_HANDLE_ERROR(retval);

    retval = addBlockToFile(store->fp, &firstBlockHeader, DEFUALT_BLOCKSIZE);
    IF_NOT_OK_HANDLE_ERROR(retval);

    dataPtr = (uint8_t*)&store->numberOfObjects;
    retval = writeToFile(store->fp, FILE_LOCATION_FOR_NUMBER_OF_OBJECTS, sizeof(uint32_t),  dataPtr);
    IF_NOT_OK_HANDLE_ERROR(retval);

    dataPtr = (uint8_t*)&store->tableOfObjectsHeader.header;
    retval = writeToFile(store->fp, FILE_LOCATION_FOR_TABLE_OF_OBJECTS_INDEX, sizeof(TDskObjIndex), dataPtr);
    IF_NOT_OK_HANDLE_ERROR(retval);

    dataPtr = (uint8_t*)&store->tashHeader.header;
    retval = writeToFile(store->fp, FILE_LOCATION_FOR_TRASH_INDEX, sizeof(TDskObjIndex), dataPtr);
    IF_NOT_OK_HANDLE_ERROR(retval);

    store->fileMode = EReadWrite;
    // ensure we return this
    (*oStore) = store;

    PROCESS_ERROR;
    if ( store && store->fp) {
        fclose(store->fp);
    }
    zfree(store);
    (*oStore) = NULL;
   FINISH;
}

void ostore_close(TOStoreHnd* oStore) {
    assert(oStore);
    assert(*oStore);
    TOStore* store = (*oStore);
    fclose(store->fp);
    zfree(store);
    *oStore = NULL;
}

// Object Inspection
int ostore_enumerateObjects(TOStoreHnd store, uint32_t* numberOfObjects) {
    assert(store != NULL);
    assert(numberOfObjects != NULL);
    (*numberOfObjects) = store->numberOfObjects;
    return 0;
}

int ostore_getObjectIdFromIndex(TOStoreHnd store, uint32_t objectIndex, TOStoreObjID* id) {
    assert(store != NULL);
    assert(id != NULL);

    START;
    assert(objectIndex < store->numberOfObjects);
    uint32_t offset = sizeof(uint32_t);

    retval = ERR_NOT_FOUND;
    for(uint32_t i = 0; i < store->numberOfObjects; i++) {
        LOCAL_MEMZ(TDskObjIndex, header);
        offset = (i * sizeof(TDskObjIndex)) + sizeof(uint32_t);
        retval = readWithIndex(store, &store->tableOfObjectsHeader.header, offset, sizeof(TDskObjIndex), &header);
        IF_NOT_OK_HANDLE_ERROR(retval);
        if (i == objectIndex) {
            (*id) = header.id;
            break;
        }
    }

    PROCESS_ERROR;

    FINISH;
}

int ostore_objectIdExists(TOStoreHnd store, TOStoreObjID id) {
    assert(store != NULL);

    START;
    uint32_t offset = sizeof(uint32_t);
    bool found = false;

    for(uint32_t i = 0; i < store->numberOfObjects; i++) {
        LOCAL_MEMZ(TDskObjIndex, header);
        offset = (i * sizeof(TDskObjIndex)) + sizeof(uint32_t);
        retval = readWithIndex(store, &store->tableOfObjectsHeader.header, offset, sizeof(TDskObjIndex), &header);
        IF_NOT_OK_HANDLE_ERROR(retval);
        if (header.id == id) {
            found = true;
            retval = ERR_OK;
            break;
        }
    }

    if ( !found ) {
        retval = ERR_NOT_FOUND;
    }

    PROCESS_ERROR;

    FINISH;
}

// Object Management
int ostore_addObjectWithId(TOStoreHnd store, TOStoreObjID id, uint32_t length) {
    assert(store != NULL);
    // check to make sure an object with the same id doesn't exist
    START;
    retval = ostore_objectIdExists(store, id);
    uint32_t blocksToAdd = 0;

    if ( retval != ERR_NOT_FOUND) {
        retval = ERR_ALREADY_EXISTS;
    } else {
        retval = ERR_OK;
    }
    IF_NOT_OK_HANDLE_ERROR(retval);

    // add the object header entry
    LOCAL_MEMZ(TDskObjIndex, index);
    index.id = id;
    index.numberOfBlocks = 0;
    index.headBlock = NO_BLOCK;
    index.tailBlock = NO_BLOCK;

    // this will auto increments the object count too.
    retval = writeObjectIndex(store, id, &index);
    IF_NOT_OK_HANDLE_ERROR(retval);

    
  // assign space to the
    blocksToAdd = REQUIRED_BLOCKS_FOR_BYTES(store, length);
    if (blocksToAdd == 0 ) blocksToAdd = 1;

    retval = growLengthWithIndex(store, &index, blocksToAdd);
    IF_NOT_OK_HANDLE_ERROR(retval);

    PROCESS_ERROR;
    FINISH;
}



int ostore_removeObject(TOStoreHnd store, TOStoreObjID id) {
    assert(store != NULL);
    // find the object with the matching id to remove
    // then compact the array to remove it
    // then update the counters

    START;
    bool found = false;
    bool startingWrite = false;
    uint32_t index = 0;
    uint32_t i = -1;
    uint32_t reducedObjectCount = store->numberOfObjects - 1;
    // uint32_t originalObjectCount = store->numberOfObjects; // think about restoring blocks on error here
    LOCAL_MEMZ(TDskObjIndex, headerToDelete);
    LOCAL_MEMZ(TDskObjectStoreBlockHeader, freeBlocks);

    for(i = 0; i < store->numberOfObjects; i++) {
        uint32_t offset = (i * sizeof(TDskObjIndex)) + sizeof(uint32_t);
        memset(&headerToDelete, 0, sizeof(TDskObjIndex));
        retval = readWithIndex(store, &store->tableOfObjectsHeader.header, offset, sizeof(TDskObjIndex), &headerToDelete);
        IF_NOT_OK_HANDLE_ERROR(retval);
        if (headerToDelete.id == id) {
            found = true;
            index = i;
            break;
        }
    }

    if (!found) {
        IF_NOT_OK_HANDLE_ERROR(ERR_NOT_FOUND);
    }

    // at this point we've found an entry to remove
    // free the blocks assocaited with the entry
    if ( headerToDelete.numberOfBlocks > 0) {
        retval = shirnkLengthWithIndex(store, &headerToDelete, headerToDelete.numberOfBlocks);
        IF_NOT_OK_HANDLE_ERROR(retval);
    }
    
    // then start updating the table
    startingWrite = true;
    for(i = index; i < reducedObjectCount; i++) {
        uint32_t offsetOld = (i * sizeof(TDskObjIndex)) + sizeof(uint32_t);
        uint32_t offsetNew = ( (i+1) * sizeof(TDskObjIndex)) + sizeof(uint32_t);
        LOCAL_MEMZ(TDskObjIndex, header);
        retval = readWithIndex(store, &store->tableOfObjectsHeader.header, offsetNew, sizeof(TDskObjIndex), &header);
        IF_NOT_OK_HANDLE_ERROR(retval);
        retval = writeWithIndex(store, &store->tableOfObjectsHeader.header, offsetOld, sizeof(TDskObjIndex), &header);
        IF_NOT_OK_HANDLE_ERROR(retval);
    }

    // update the counters
    store->numberOfObjects--;
    // save to disk
    retval = writeObjectCount(store);
    IF_NOT_OK_HANDLE_ERROR(retval);

    PROCESS_ERROR;
    // ugh... ok, in this situation we need to restore the missing data
    // easiest way to do this is to have a copy of the modified data and
    // restore it.
    // this would be:
    //  - the entry being deleted (as it is first to be overwritten)
    //    : headerToDelete
    //  - the clobber loop simply produces duplicates of entries on failure
    //    if we find a
    // search for
    if (startingWrite) {

    }
    FINISH;

}

// Object Operations
int ostore_setLength(TOStoreHnd store, TOStoreObjID id, uint32_t lengthRequested) {
    assert(store);
    START;
    LOCAL_MEMZ(TDskObjIndex, head);
    retval = readObjectIndex(store, id, &head);
    IF_NOT_OK_HANDLE_ERROR(retval);
    retval = setLengthWithIndex(store, &head, lengthRequested);
    PROCESS_ERROR;
    FINISH;
}

int ostore_getLength(TOStoreHnd store, TOStoreObjID id, uint32_t* length) {
    assert(store);
    assert(id != OBJECT_TABLE_ID || id != TRASH_TABLE_ID);
    assert(length);
    (*length) = 0;
    START;


    LOCAL_MEMZ(TDskObjIndex, head);
    retval = readObjectIndex(store, id, &head);
    IF_NOT_OK_HANDLE_ERROR(retval);

    (*length) = head.numberOfBlocks * store->fileHeader.header.blockSize;

    PROCESS_ERROR;

    FINISH;

}

// Reading and Writing Data
int ostore_read(TOStoreHnd store, TOStoreObjID id, uint32_t position, uint32_t length, void* destination) {
    assert(store);
    assert(id != OBJECT_TABLE_ID || id != TRASH_TABLE_ID);
    assert(destination);

    START;
    LOCAL_MEMZ(TDskObjIndex, head);
    retval = readObjectIndex(store, id, &head);
    IF_NOT_OK_HANDLE_ERROR(retval);

    /// read data now with header.
    retval = readWithIndex(store, &head, position, length, destination);

    PROCESS_ERROR;
    FINISH;
}

int ostore_write(TOStoreHnd store, TOStoreObjID id, uint32_t position, const void* source, uint32_t length) {
    assert(store);
    assert(id != OBJECT_TABLE_ID || id != TRASH_TABLE_ID);
    assert(source);

    START;
    uint32_t totalLength = 0;
    uint32_t availableSpace = 0;
    uint8_t* dataPtr = NULL;

    LOCAL_MEMZ(TDskObjIndex, head);
    retval = readObjectIndex(store, id, &head);
    IF_NOT_OK_HANDLE_ERROR(retval);

    // if there are more blocks needed, than are available, assert, this should be checked before invoking.
    totalLength = position + length;
    availableSpace = store->fileHeader.header.blockSize * head.numberOfBlocks;
    assert(totalLength < availableSpace);
    dataPtr = (uint8_t*)source;
    retval = writeWithIndex(store, &head, position, length, dataPtr);

    PROCESS_ERROR;
    FINISH;
}

int ostore_version_major() {
#ifdef FULL_LIB_BUILD    
    return ostore_VERSION_MAJOR;
#else
    return 0;
#endif
}

int ostore_version_minor() {
#ifdef FULL_LIB_BUILD
    return ostore_VERSION_MINOR;
#else
    return 0;
#endif
}