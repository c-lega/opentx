/*
 * Copyright (C) OpenTX
 *
 * Based on code named
 *   th9x - http://code.google.com/p/th9x
 *   er9x - http://code.google.com/p/er9x
 *   gruvin9x - http://code.google.com/p/gruvin9x
 *
 * License GPLv2: http://www.gnu.org/licenses/gpl-2.0.html
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "opentx.h"
#include "otx_file.h"
#include "modelslist.h"

void getModelPath(char * path, const char * filename)
{
  strcpy(path, STR_MODELS_PATH);
  path[sizeof(MODELS_PATH)-1] = '/';
  strcpy(&path[sizeof(MODELS_PATH)], filename);
}

const char * writeFile(const char * filename, const uint8_t * data, uint16_t size)
{
  TRACE("writeFile(%s)", filename);
  
  FIL file;
  unsigned char buf[8];
  UINT written;

  FRESULT result = f_open(&file, filename, FA_CREATE_ALWAYS | FA_WRITE);
  if (result != FR_OK) {
    return SDCARD_ERROR(result);
  }

  *(uint32_t*)&buf[0] = OTX_FOURCC;
  buf[4] = EEPROM_VER;
  buf[5] = 'M';
  *(uint16_t*)&buf[6] = size;

  result = f_write(&file, buf, 8, &written);
  if (result != FR_OK || written != 8) {
    f_close(&file);
    return SDCARD_ERROR(result);
  }

  result = f_write(&file, data, size, &written);
  if (result != FR_OK || written != size) {
    f_close(&file);
    return SDCARD_ERROR(result);
  }

  f_close(&file);
  return NULL;
}

const char * writeModel()
{
  char path[256];
  getModelPath(path, g_eeGeneral.currModelFilename);
  return writeFile(path, (uint8_t *)&g_model, sizeof(g_model));
}

const char * loadFile(const char * filename, uint8_t * data, uint16_t maxsize)
{
  TRACE("loadFile(%s)", filename);
  
  FIL file;
  char buf[8];
  UINT read;

  FRESULT result = f_open(&file, filename, FA_OPEN_EXISTING | FA_READ);
  if (result != FR_OK) {
    return SDCARD_ERROR(result);
  }

  if (f_size(&file) < 8) {
    f_close(&file);
    return STR_INCOMPATIBLE;
  }

  result = f_read(&file, (uint8_t *)buf, 8, &read);
  if (result != FR_OK || read != 8) {
    f_close(&file);
    return SDCARD_ERROR(result);
  }

  uint8_t version = (uint8_t)buf[4];
  if ((*(uint32_t*)&buf[0] != OTX_FOURCC && *(uint32_t*)&buf[0] != O9X_FOURCC) || version < FIRST_CONV_EEPROM_VER || version > EEPROM_VER || buf[5] != 'M') {
    f_close(&file);
    return STR_INCOMPATIBLE;
  }

  uint16_t size = min<uint16_t>(maxsize, *(uint16_t*)&buf[6]);
  result = f_read(&file, data, size, &read);
  if (result != FR_OK || read != size) {
    f_close(&file);
    return SDCARD_ERROR(result);
  }

  f_close(&file);
  return NULL;
}

const char * readModel(const char * filename, uint8_t * buffer, uint32_t size)
{
  char path[256];
  getModelPath(path, filename);
  return loadFile(path, buffer, size);
}

const char * loadModel(const char * filename, bool alarms)
{
  preModelLoad();

  const char * error = readModel(filename, (uint8_t *)&g_model, sizeof(g_model));
  if (error) {
    TRACE("loadModel error=%s", error);
  }
  
  if (error) {
    modelDefault(0) ;
    storageCheck(true);
    alarms = false;
  }

  postModelLoad(alarms);

  return error;
}

const char * loadRadioSettingsSettings()
{
  const char * error = loadFile(RADIO_SETTINGS_PATH, (uint8_t *)&g_eeGeneral, sizeof(g_eeGeneral));
  if (error) {
    TRACE("loadRadioSettingsSettings error=%s", error);
    __builtin_trap();
  }
  // TODO this is temporary, we only have one model for now
  return error;
}

const char * writeGeneralSettings()
{
  return writeFile(RADIO_SETTINGS_PATH, (uint8_t *)&g_eeGeneral, sizeof(g_eeGeneral));
}

void storageCheck(bool immediately)
{
  if (storageDirtyMsk & EE_GENERAL) {
    TRACE("eeprom write general");
    storageDirtyMsk -= EE_GENERAL;
    const char * error = writeGeneralSettings();
    if (error) {
      TRACE("writeGeneralSettings error=%s", error);
    }
  }

  if (storageDirtyMsk & EE_MODEL) {
    TRACE("eeprom write model");
    storageDirtyMsk -= EE_MODEL;
    const char * error = writeModel();
    if (error) {
      TRACE("writeModel error=%s", error);
    }
  }
}

void storageReadAll()
{
  TRACE("storageReadAll");
  
  if (loadRadioSettingsSettings() != NULL) {
    storageEraseAll(true);
  }

#if defined(CPUARM)
  for (uint8_t i=0; languagePacks[i]!=NULL; i++) {
    if (!strncmp(g_eeGeneral.ttsLanguage, languagePacks[i]->id, 2)) {
      currentLanguagePackIdx = i;
      currentLanguagePack = languagePacks[i];
    }
  }
#endif

  if (loadModel(g_eeGeneral.currModelFilename, false) != NULL) {
    sdCheckAndCreateDirectory(MODELS_PATH);
    createModel();
  }
}

void storageCreateModelsList()
{
  FIL file;

  FRESULT result = f_open(&file, RADIO_MODELSLIST_PATH, FA_CREATE_ALWAYS | FA_WRITE);
  if (result == FR_OK) {
    f_puts("[" DEFAULT_CATEGORY "]\n" DEFAULT_MODEL_FILENAME "\n", &file);
    f_close(&file);
  }
}

void storageFormat()
{
  sdCheckAndCreateDirectory(RADIO_PATH);
  sdCheckAndCreateDirectory(MODELS_PATH);
  storageCreateModelsList();
}

const char * createModel()
{
  preModelLoad();

  char filename[LEN_MODEL_FILENAME+1];
  memset(filename, 0, sizeof(filename));
  strcpy(filename, "model.bin");

  int index = findNextFileIndex(filename, LEN_MODEL_FILENAME, MODELS_PATH);
  if (index > 0) {
    modelDefault(index);
    memcpy(g_eeGeneral.currModelFilename, filename, sizeof(g_eeGeneral.currModelFilename));
    storageDirty(EE_GENERAL);
    storageDirty(EE_MODEL);
    storageCheck(true);
  }
  postModelLoad(false);

  return g_eeGeneral.currModelFilename;
}

void storageEraseAll(bool warn)
{
  TRACE("storageEraseAll");

#if defined(COLORLCD)
  // the theme has not been loaded before
  theme->load();
#endif

  generalDefault();
  modelDefault(1);

  if (warn) {
    ALERT(STR_STORAGE_WARNING, STR_BAD_RADIO_DATA, AU_BAD_RADIODATA);
  }

  RAISE_ALERT(STR_STORAGE_WARNING, STR_STORAGE_FORMAT, NULL, AU_NONE);

  storageFormat();
  storageDirty(EE_GENERAL|EE_MODEL);
  storageCheck(true);
}

void createRadioBackup()
{
    char otx_file[60];
    char model_file[60];

    //rco: is that really necessary?
    g_eeGeneral.unexpectedShutdown = 0;
    storageDirty(EE_GENERAL);
    storageCheck(true);

    // create the directory if needed...
    const char * error = sdCheckAndCreateDirectory(BACKUPS_PATH);
    if (error) {
        POPUP_WARNING(error);
        return;
    }
    
    // prepare the filename...
    char * tmp = strAppend(otx_file, BACKUPS_PATH "/backup");
#if defined(RTCLOCK)
    tmp = strAppendDate(tmp, true);
#endif
    strAppend(tmp, BACKUP_EXT);

    ModelsList models;
    if (!models.load()) {
        //TODO: show some error
        POPUP_WARNING("Backup");
        SET_WARNING_INFO("Could not load model list!", 0, 0);
        return;
    }

    // total files = models + radio.bin + model list
    int nb_files = models.modelsCount + 2;
    
    if (!initOtxWriter(otx_file)) {
        closeOtxWriter();
        sdDelete(otx_file);
        //TODO: show some error
        POPUP_WARNING("Backup");
        SET_WARNING_INFO("Backup failed!", 0, 0);
        return;
    }

    drawProgressBar("Backup", 0, nb_files);

    std::list<ModelsCategory*>::iterator cat_it;
    std::list<ModelCell*>::iterator      mod_it;

    for(int i=0; i<nb_files; i++) {

        bool result = false;
        if (i == 0) {
            result = addFile2Otx(RADIO_SETTINGS_PATH);
        }
        else if (i == 1) {
            result = addFile2Otx(RADIO_MODELSLIST_PATH);
        }
        else {

            // jump over empty categories
            // assume there is at least one model, otherwise, we wouldn't be here...
#define GET_NEXT_MODEL() {                              \
                while((*cat_it)->empty()) cat_it++;     \
                mod_it = (*cat_it)->begin();            \
            }
            
            if (i == 2) {
                cat_it = models.categories.begin();
                GET_NEXT_MODEL();
            }
            else if (mod_it == (*cat_it)->end()) {
                cat_it++;
                GET_NEXT_MODEL();
            }

            wdt_reset();
            getModelPath(model_file, (*mod_it)->modelFilename);
            result = addFile2Otx(model_file);
            mod_it++;
        }

        if (!result) {
            closeOtxWriter();
            sdDelete(otx_file);
            //TODO: display some error here...
            POPUP_WARNING("Backup");
            SET_WARNING_INFO("Error while writing OTX file", 0, 0);
            return;
        }

        drawProgressBar("Backup", i+1, nb_files);
    }

    closeOtxWriter();
    POPUP_INFO("Backup");
    SET_WARNING_INFO("Completed sucessfully", 0, 0);
    
    return;
}

void restoreRadioBackup(const char * filename)
{
    //TODO: ask for confirmation???

    // stop everything while restoring...
    pausePulses();
    opentxClose(false);

    // re-mount SD card (stopped by opentxClose())
    sdMount();
    
    bool result = true;
    int  nfiles = initOtxReader(filename);

    wdt_reset();

    if ((nfiles <= 0) ||
        (locateFileInOtx(RADIO_SETTINGS_PATH) < 0) ||
        (locateFileInOtx(RADIO_MODELSLIST_PATH) < 0)) {

        result = false;
    }
    else {
        for (unsigned int i=0; i < (unsigned)nfiles; i++) {
            wdt_reset();
            result = result && extractFileFromOtx(i);
        }
    }

    closeOtxReader();
    POPUP_WARNING("Restore");

    if (!result) {
        SET_WARNING_INFO("Restore failed!", 0, 0);
    }
    else {
        SET_WARNING_INFO("Completed sucessfully", 0, 0);
    }

    // unmount SD card (opentxResume() will mount it again)
    sdDone();

    // and restart everything
    opentxResume();
    resumePulses();
}
