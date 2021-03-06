//IBM_PROLOG_BEGIN_TAG
/* 
 * Copyright 2003,2016 IBM International Business Machines Corp.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * 	http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 * implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
//IBM_PROLOG_END_TAG


//----------------------------------------------------------------------
//  Includes
//----------------------------------------------------------------------
#include <stdio.h>
#include <string.h>
#include <fstream>

#include <ecmdCommandUtils.H>
#include <ecmdReturnCodes.H>
#include <ecmdClientCapi.H>
#include <ecmdUtils.H>
#include <ecmdDataBuffer.H>
#include <ecmdInterpreter.H>
#include <ecmdSharedUtils.H>

//----------------------------------------------------------------------
//  User Types
//----------------------------------------------------------------------


//----------------------------------------------------------------------
//  Constants
//----------------------------------------------------------------------

//----------------------------------------------------------------------
//  Macros
//----------------------------------------------------------------------

//----------------------------------------------------------------------
//  Internal Function Prototypes
//----------------------------------------------------------------------


//----------------------------------------------------------------------
//  Global Variables
//----------------------------------------------------------------------

//---------------------------------------------------------------------
// Member Function Specifications
//---------------------------------------------------------------------
#ifndef ECMD_REMOVE_VPD_FUNCTIONS
uint32_t ecmdGetVpdKeywordUser(int argc, char * argv[]) {

  uint32_t rc = ECMD_SUCCESS , coeRc = ECMD_SUCCESS;
  ecmdLooperData looperdata;            ///< Store internal Looper data
  std::string outputformat = "xl";      ///< Output Format to display
  ecmdChipTarget target;                ///< Current target operating on
  ecmdDataBuffer data;                  ///< Data from the module vpd record
  ecmdDataBuffer vpdImage;              ///< buffer to load a VPD image into, if given
  bool validPosFound = false;           ///< Did the looper find anything to execute on
  bool outputformatflag = false;
  std::ofstream ops;                    ///< Output stream for writing vpd data into
  std::string newFilename;              ///< filename with target postfix incase of multi positions
  ecmdChipTarget target1;               ///< Current target operating on-for second looper
  ecmdLooperData looperdata1;           ///< looper to do the real work
  char targetStr[50];                   ///< target postfix for the filename incase of multi positions
  int targetCount=0;                    ///< counts the number of targets user specified
  int rid = -1;                         ///< rid of device to operate on
  int match;                            ///< For sscanf
  
  /************************************************************************/
  /* Parse Local FLAGS here!                                              */
  /************************************************************************/
  /* get format flag, if it's there */
  char * formatPtr = ecmdParseOptionWithArgs(&argc, &argv, "-o");
  if (formatPtr != NULL) {
    outputformat = formatPtr;
    outputformatflag = true;
  }
  
  /* get VPD file to write to, if it's there */
  char * vpdImageFile = ecmdParseOptionWithArgs(&argc, &argv, "-vpdimage");

  /* Get the filename if -fb is specified */
  char * filename = ecmdParseOptionWithArgs(&argc, &argv, "-fb");
  std::string printed;

  if((filename != NULL) && (outputformatflag) ) {
    printed = "getvpdkeyword - Options '-fb' cannot be specified with '-o' option.\n";
    ecmdOutputError(printed.c_str());
    return ECMD_INVALID_ARGS;
  } 
  /************************************************************************/
  /* Parse Common Cmdline Args                                            */
  /************************************************************************/
  rc = ecmdCommandArgs(&argc, &argv);
  if (rc) return rc;

  /* Global args have been parsed, we can read if -coe was given */
  bool coeMode = ecmdGetGlobalVar(ECMD_GLOBALVAR_COEMODE); ///< Are we in continue on error mode

  /************************************************************************/
  /* Parse Local ARGS here!                                               */
  /************************************************************************/
  if (argc < 5) {  
    ecmdOutputError("getvpdkeyword - Too few arguments specified; you need at least a chip/rid, vpdtype, recordname, keyword and numbytes.\n");
    ecmdOutputError("getvpdkeyword - Type 'getvpdkeyword -h' for usage.\n");
    return ECMD_INVALID_ARGS;
  } else if (argc > 5) {
    ecmdOutputError("getvpdkeyword - Too many arguments specified; you only need chip/rid, vpdtype, recordname, keyword and numbytes.\n");
    ecmdOutputError("getvpdkeyword - Type 'getvpdkeyword -h' for usage.\n");
    return ECMD_INVALID_ARGS;
  }

  //Check to see if rid was passed in instead of a target
  if (ecmdIsAllHex(argv[0])) {
    match = sscanf(argv[0], "%x", &rid);
    if (match != 1) {
      ecmdOutputError("Error occurred processing rid!\n");
      return ECMD_INVALID_ARGS;
    }
  }

  //Setup target if rid is not used
  if (rid == -1) {
    //Setup the target that will be used to query the system config
    std::string chipType, chipUnitType;
    ecmdParseChipField(argv[0], chipType, chipUnitType);
    if (chipUnitType != "") {
      ecmdOutputError("getvpdkeyword - chipUnit specified on the command line, this function doesn't support chipUnits.\n");
      return ECMD_INVALID_ARGS;
    }
    target.chipType = chipType;
    if (target.chipType == "nochip") {
      target.chipTypeState = ECMD_TARGET_FIELD_UNUSED;
      target.posState = ECMD_TARGET_FIELD_UNUSED;
    } else {
      target.chipTypeState = ECMD_TARGET_FIELD_VALID;
      target.posState = ECMD_TARGET_FIELD_WILDCARD;
    }
    target.cageState = target.nodeState = target.slotState = ECMD_TARGET_FIELD_WILDCARD;
    target.chipUnitTypeState = target.chipUnitNumState = target.threadState = ECMD_TARGET_FIELD_UNUSED;

    target1 = target; //Created for the second looper needed for -fb case with multiple positions
  }

  char *vpdType = argv[1];
  if (!strcasecmp(vpdType, "MOD") && !strcasecmp(vpdType, "FRU")) {
    ecmdOutputError("getvpdkeyword - You have to specify either mod or fru for the vpd type\n");
    return ECMD_INVALID_ARGS;
  }
  if ((rid != -1) && strcasecmp(vpdType, "FRU")) {
    ecmdOutputError("getvpdkeyword - rid may only be specified with fru for the vpd type\n");
    return ECMD_INVALID_ARGS;
  }

  char *recordName = argv[2];
  char *keyWord = argv[3];
  
  if (!ecmdIsAllDecimal(argv[4])) {
    ecmdOutputError("getvpdkeyword - Non-decimal numbers detected in numBytes field\n");
    return ECMD_INVALID_ARGS;
  }
  
  uint32_t numBytes = (uint32_t)atoi(argv[4]);
  
  // If a VPD image to use was given, read it in now
  if (vpdImageFile != NULL) {
    rc = vpdImage.readFile(vpdImageFile, ECMD_SAVE_FORMAT_BINARY_DATA);
    if (rc) {
      printed = "putvpdkeyword - Problems occurred in reading image file " + (std::string)vpdImageFile + "\n";
      ecmdOutputError(printed.c_str()); 
      return rc;
    }
  }

  if (rid == -1) {
    //Run the loop to Check the number of targets
    if (filename != NULL) {
      rc = ecmdLooperInit(target, ECMD_SELECTED_TARGETS_LOOP, looperdata);
      if (rc) return rc;
 
      while ( ecmdLooperNext(target, looperdata) ) {
        targetCount++;
      }
    }
  
    //Looper to do the actual work
    rc = ecmdLooperInit(target1, ECMD_SELECTED_TARGETS_LOOP, looperdata1);
    if (rc) return rc;
              
    while (ecmdLooperNext(target1, looperdata1) && (!coeRc || coeMode)) {

      if (!strcasecmp(vpdType, "MOD")) {
        if (vpdImageFile != NULL) {
          rc = getModuleVpdKeywordFromImage(target1, recordName, keyWord, numBytes, vpdImage, data);
        } else {
          rc = getModuleVpdKeyword(target1, recordName, keyWord, numBytes, data);
        }
      } else {
        if (vpdImageFile != NULL) {
          rc = getFruVpdKeywordFromImage(target1, recordName, keyWord, numBytes, vpdImage, data);
        } else {
          rc = getFruVpdKeyword(target1, recordName, keyWord, numBytes, data);
        }
      }
      if (rc) {
        printed = "getvpdkeyword - Error occurred performing ";
        printed += (!strcasecmp(vpdType, "MOD") ? "getModuleVpdKeyword" : "getFruVpdKeyword");
        printed += " on " + ecmdWriteTarget(target1) + "\n";
        ecmdOutputError( printed.c_str() );
        coeRc = rc;
        continue;
      }
      else {
        validPosFound = true;     
      }

      printed = ecmdWriteTarget(target1) + "\n";
      if (filename != NULL) {
        if (targetCount > 1) {
          sprintf(targetStr, "k%dn%ds%dp%d", target1.cage, target1.node, target1.slot, target1.pos); 
          newFilename = (std::string)filename+"."+(std::string)targetStr;
        }
        else { newFilename = (std::string)filename; }
      
        rc = data.writeFile(newFilename.c_str(), ECMD_SAVE_FORMAT_BINARY_DATA);
     
        if (rc) {
         printed += "getvpdkeyword - Problems occurred writing data into file " + newFilename + "\n";
         ecmdOutputError(printed.c_str()); 
         return rc;
        }
        ecmdOutput( printed.c_str() );
      
        ops.close();
      } 
      else {
        std::string dataStr = ecmdWriteDataFormatted(data, outputformat);
        printed += dataStr;
        ecmdOutput( printed.c_str() );
      } 
    
    }
  } else {
      if (!strcasecmp(vpdType, "MOD")) {
         // ERROR
      } else {
        rc = getFruVpdKeywordWithRid(rid, recordName, keyWord, numBytes, data);
        validPosFound = true;     
      }

      printed = argv[0];
      printed += "\n";
      if (filename != NULL) {
        newFilename = (std::string)filename;

        rc = data.writeFile(newFilename.c_str(), ECMD_SAVE_FORMAT_BINARY_DATA);

        if (rc) {
         printed += "getvpdkeyword - Problems occurred writing data into file " + newFilename + "\n";
         ecmdOutputError(printed.c_str());
         return rc;
        }
        ecmdOutput( printed.c_str() );

        ops.close();
      }
      else {
        std::string dataStr = ecmdWriteDataFormatted(data, outputformat);
        printed += dataStr;
        ecmdOutput( printed.c_str() );
      }
  }
  // coeRc will be the return code from in the loop, coe mode or not.
  if (coeRc) return coeRc;

  // This is an error common across all UI functions
  if (!validPosFound) {
    ecmdOutputError("getvpdkeyword - Unable to find a valid chip to execute command on\n");
    return ECMD_TARGET_NOT_CONFIGURED;
  }

  return rc;
}

uint32_t ecmdPutVpdKeywordUser(int argc, char * argv[]) {

  uint32_t rc = ECMD_SUCCESS , coeRc = ECMD_SUCCESS;
  ecmdLooperData looperdata;            ///< Store internal Looper data
  std::string inputformat = "xl";       ///< format of input data
  ecmdChipTarget target;                ///< Current target operating on
  ecmdDataBuffer data;                  ///< buffer for the Data to write into the module vpd keyword
  ecmdDataBuffer vpdImage;              ///< buffer to load a VPD image into, if given
  bool validPosFound = false;           ///< Did the looper find anything to execute on
  bool inputformatflag = false;
  int rid = -1;                         ///< rid of device to operate on
  int match;                            ///< For sscanf

  /************************************************************************/
  /* Parse Local FLAGS here!                                              */
  /************************************************************************/
  /* get format flag, if it's there */
  char * formatPtr = ecmdParseOptionWithArgs(&argc, &argv, "-i");
  if (formatPtr != NULL) {
    inputformat = formatPtr;
    inputformatflag = true;
  }

  /* get VPD file to write to, if it's there */
  char * vpdImageFile = ecmdParseOptionWithArgs(&argc, &argv, "-vpdimage");
  
  /* Get the filename if -fb is specified */
  char * filename = ecmdParseOptionWithArgs(&argc, &argv, "-fb");
  std::string printed;

  if((filename != NULL) && (inputformatflag) ) {
    printed = "putvpdkeyword - Options '-fb' cannot be specified with '-i' option.\n";
    ecmdOutputError(printed.c_str());
    return ECMD_INVALID_ARGS;
  } 
  /************************************************************************/
  /* Parse Common Cmdline Args                                            */
  /************************************************************************/
  rc = ecmdCommandArgs(&argc, &argv);
  if (rc) return rc;

  /* Global args have been parsed, we can read if -coe was given */
  bool coeMode = ecmdGetGlobalVar(ECMD_GLOBALVAR_COEMODE); ///< Are we in continue on error mode

  /************************************************************************/
  /* Parse Local ARGS here!                                               */
  /************************************************************************/
  if ((argc < 5) && (filename == NULL)) {  
    ecmdOutputError("putvpdkeyword - Too few arguments specified; you need at least a chip/rid, vpdtype, recordname, keyword and data.\n");
    ecmdOutputError("putvpdkeyword - Type 'putvpdkeyword -h' for usage.\n");
    return ECMD_INVALID_ARGS;
  } else if ((argc < 4) && (filename != NULL)) {
    ecmdOutputError("putvpdkeyword - Too few arguments specified; you need at least a chip/rid, vpdtype, recordname, keyword and input file.\n");
    ecmdOutputError("putvpdkeyword - Type 'putvpdkeyword -h' for usage.\n");
    return ECMD_INVALID_ARGS;
  } else if ((argc > 4) && (filename != NULL)) {
    ecmdOutputError("putvpdkeyword - Too many arguments specified; you need a chip/rid, vpdtype, recordname, keyword and input file.\n");
    ecmdOutputError("putvpdkeyword - Type 'putvpdkeyword -h' for usage.\n");
    return ECMD_INVALID_ARGS;
  } else if ((argc > 5) && (filename == NULL)) {
    ecmdOutputError("putvpdkeyword - Too many arguments specified; you need a chip/rid, vpdtype, recordname, keyword and data.\n");
    ecmdOutputError("putvpdkeyword - Type 'putvpdkeyword -h' for usage.\n");
    return ECMD_INVALID_ARGS;
  }

  //Check to see if rid was passed in instead of a target
  if (ecmdIsAllHex(argv[0])) {
    match = sscanf(argv[0], "%x", &rid);
    if (match != 1) {
      ecmdOutputError("Error occurred processing rid!\n");
      return ECMD_INVALID_ARGS;
    }
  }

  //Setup target if rid is not used
  if (rid == -1) {
    //Setup the target that will be used to query the system config 
    std::string chipType, chipUnitType;
    ecmdParseChipField(argv[0], chipType, chipUnitType);
    if (chipUnitType != "") {
      ecmdOutputError("getvpdkeyword - chipUnit specified on the command line, this function doesn't support chipUnits.\n");
      return ECMD_INVALID_ARGS;
    }
    target.chipType = chipType;
  if (target.chipType == "nochip") {
      target.chipTypeState = ECMD_TARGET_FIELD_UNUSED;
      target.posState = ECMD_TARGET_FIELD_UNUSED;
    } else {
      target.chipTypeState = ECMD_TARGET_FIELD_VALID;
      target.posState = ECMD_TARGET_FIELD_WILDCARD;
    }
    target.cageState = target.nodeState = target.slotState = ECMD_TARGET_FIELD_WILDCARD;
    target.chipUnitTypeState = target.chipUnitNumState = target.threadState = ECMD_TARGET_FIELD_UNUSED;
  }

  char *vpdType = argv[1];
  if (!strcasecmp(vpdType, "MOD") && !strcasecmp(vpdType, "FRU")) {
    ecmdOutputError("putvpdkeyword - You have to specify either mod or fru for the vpd type\n");
    return ECMD_INVALID_ARGS;
  }
  if ((rid != -1) && strcasecmp(vpdType, "FRU")) {
    ecmdOutputError("putvpdkeyword - rid may only be specified with fru for the vpd type\n");
    return ECMD_INVALID_ARGS;
  }

  char *recordName = argv[2];
  char *keyWord = argv[3];
  
  if (filename != NULL) {
    rc = data.readFile(filename, ECMD_SAVE_FORMAT_BINARY_DATA);
    if (rc) {
      printed = "putvpdkeyword - Problems occurred in reading data from file " + (std::string)filename + "\n";
      ecmdOutputError(printed.c_str()); 
      return rc;
    }
  } else  {
    //container to store data
    rc = ecmdReadDataFormatted(data, argv[4], inputformat);
    if (rc) {
      ecmdOutputError("putvpdkeyword - Problems occurred parsing input data, must be an invalid format\n");
      return rc;
    }
  }
  
  // If a VPD image to use was given, read it in now
  if (vpdImageFile != NULL) {
    rc = vpdImage.readFile(vpdImageFile, ECMD_SAVE_FORMAT_BINARY_DATA);
    if (rc) {
      printed = "putvpdkeyword - Problems occurred in reading image file " + (std::string)vpdImageFile + "\n";
      ecmdOutputError(printed.c_str()); 
      return rc;
    }
  }

  if (rid == -1) {
    /************************************************************************/
    /* Kickoff Looping Stuff                                                */
    /************************************************************************/
    rc = ecmdLooperInit(target, ECMD_SELECTED_TARGETS_LOOP, looperdata);
    if (rc) return rc;
  
    while (ecmdLooperNext(target, looperdata) && (!coeRc || coeMode)) {

      if (!strcasecmp(vpdType, "MOD")) {
        if (vpdImageFile != NULL) {
          rc = putModuleVpdKeywordToImage(target, recordName, keyWord, vpdImage, data);
        } else {
          rc = putModuleVpdKeyword(target, recordName, keyWord, data);
        }        
      } else {
        if (vpdImageFile != NULL) {
          rc = putFruVpdKeywordToImage(target, recordName, keyWord, vpdImage, data);
        } else {
          rc = putFruVpdKeyword(target, recordName, keyWord, data);
        }
      }
      if (rc) {
        printed = "putvpdkeyword - Error occurred performing putModuleVpdKeyword ";
        printed += (!strcasecmp(vpdType, "MOD") ? "putModuleVpdKeyword" : "putFruVpdKeyword");
        printed += " on " + ecmdWriteTarget(target) + "\n";
        ecmdOutputError( printed.c_str() );
        coeRc = rc;
        continue;
        //return rc;
      }
      else {
        validPosFound = true;     
      }

      if (!ecmdGetGlobalVar(ECMD_GLOBALVAR_QUIETMODE)) {
        printed = ecmdWriteTarget(target) + "\n";
        ecmdOutput(printed.c_str());
      }
    }
  } else {
    if (!strcasecmp(vpdType, "MOD")) {
       // ERROR
    } else {
      rc = putFruVpdKeywordWithRid(rid, recordName, keyWord, data);
      validPosFound = true;
    }
    if (rc) {
      printed = "putvpdkeyword - Error occurred performing putModuleVpdKeyword ";
      printed += (!strcasecmp(vpdType, "MOD") ? "putModuleVpdKeyword" : "putFruVpdKeyword");
      printed += " with rid ";
      printed += argv[0];
      printed += "\n";
      ecmdOutputError( printed.c_str() );
      coeRc = rc;
    }
    else {
      validPosFound = true;     
    }

    if (!ecmdGetGlobalVar(ECMD_GLOBALVAR_QUIETMODE)) {
      printed = argv[0];
      printed += "\n";
      ecmdOutput(printed.c_str());
    }
  }
  // coeRc will be the return code from in the loop, coe mode or not.
  if (coeRc) return coeRc;

  // If a VPD image to use was given, now write the changes back out to the file given for use
  if (vpdImageFile != NULL) {
    rc = vpdImage.writeFile(vpdImageFile, ECMD_SAVE_FORMAT_BINARY_DATA);
    if (rc) {
      printed = "putvpdkeyword - Problems occurred in writing image file " + (std::string)vpdImageFile + "\n";
      ecmdOutputError(printed.c_str()); 
      return rc;
    }
  }

  // This is an error common across all UI functions
  if (!validPosFound) {
    ecmdOutputError("putvpdkeyword - Unable to find a valid chip to execute command on\n");
    return ECMD_TARGET_NOT_CONFIGURED;
  }

  return rc;
}

uint32_t ecmdPutVpdImageUser(int argc, char * argv[]) {

  uint32_t rc = ECMD_SUCCESS , coeRc = ECMD_SUCCESS;
  ecmdLooperData looperdata;            ///< Store internal Looper data
  std::string inputformat = "xl";       ///< format of input data
  ecmdChipTarget target;                ///< Current target operating on
  ecmdDataBuffer data;                  ///< buffer for the Data to write into the module vpd keyword
  bool validPosFound = false;           ///< Did the looper find anything to execute on
  bool inputformatflag = false;
  /************************************************************************/
  /* Parse Local FLAGS here!                                              */
  /************************************************************************/
  
  /* get format flag, if it's there */
  char * formatPtr = ecmdParseOptionWithArgs(&argc, &argv, "-i");
  if (formatPtr != NULL) {
    inputformat = formatPtr;
    inputformatflag = true;
  }
  
  /* Get the filename if -fb is specified */
  char * filename = ecmdParseOptionWithArgs(&argc, &argv, "-fb");
  std::string printed;

  if((filename != NULL) && (inputformatflag) ) {
    printed = "putvpdimage - Options '-fb' cannot be specified with '-i' option.\n";
    ecmdOutputError(printed.c_str());
    return ECMD_INVALID_ARGS;
  } 
  /************************************************************************/
  /* Parse Common Cmdline Args                                            */
  /************************************************************************/

  rc = ecmdCommandArgs(&argc, &argv);
  if (rc) return rc;

  /* Global args have been parsed, we can read if -coe was given */
  bool coeMode = ecmdGetGlobalVar(ECMD_GLOBALVAR_COEMODE); ///< Are we in continue on error mode


  /************************************************************************/
  /* Parse Local ARGS here!                                               */
  /************************************************************************/
  if ((argc < 3) && (filename == NULL)) {  
    ecmdOutputError("putvpdimage - Too few arguments specified; you need to at least specify the chip, vpdtype and data to write.\n");
    ecmdOutputError("putvpdimage - Type 'putvpdimage -h' for usage.\n");
    return ECMD_INVALID_ARGS;
  } else if ((argc > 2) && (filename != NULL)) {
    ecmdOutputError("putvpdimage - Too many arguments specified; you only need to specify chip, vpdtype and data OR input file.\n");
    ecmdOutputError("putvpdimage - Type 'putvpdimage -h' for usage.\n");
    return ECMD_INVALID_ARGS;
  } else if ((argc > 3) && (filename == NULL)) {
    ecmdOutputError("putvpdimage - Too many arguments specified; you only need to specify chip, vpdtype and data OR input file.\n");
    ecmdOutputError("putvpdimage - Type 'putvpdimage -h' for usage.\n");
    return ECMD_INVALID_ARGS;
  } 

  //Setup the target that will be used to query the system config 
  std::string chipType, chipUnitType;
  ecmdParseChipField(argv[0], chipType, chipUnitType);
  if (chipUnitType != "") {
    ecmdOutputError("getvpdkeyword - chipUnit specified on the command line, this function doesn't support chipUnits.\n");
    return ECMD_INVALID_ARGS;
  }
  target.chipType = chipType;
  if (target.chipType == "nochip") {
    target.chipTypeState = ECMD_TARGET_FIELD_UNUSED;
    target.posState = ECMD_TARGET_FIELD_UNUSED;
  } else {
    target.chipTypeState = ECMD_TARGET_FIELD_VALID;
    target.posState = ECMD_TARGET_FIELD_WILDCARD;
  }
  target.cageState = target.nodeState = target.slotState = ECMD_TARGET_FIELD_WILDCARD;
  target.chipUnitTypeState = target.chipUnitNumState = target.threadState = ECMD_TARGET_FIELD_UNUSED;

  char *vpdType = argv[1];
  if (!strcasecmp(vpdType, "MOD") && !strcasecmp(vpdType, "FRU")) {
    ecmdOutputError("putvpdimage - You have to specify either mod or fru for the vpd type\n");
    return ECMD_INVALID_ARGS;
  }

  if (filename != NULL) {
    rc = data.readFile(filename, ECMD_SAVE_FORMAT_BINARY_DATA);
    if (rc) {
      printed = "putvpdimage - Problems occurred in reading data from file " + (std::string)filename + "\n";
      ecmdOutputError(printed.c_str()); 
      return rc;
    }
  } else  {
    //container to store data
    rc = ecmdReadDataFormatted(data, argv[2], inputformat);
    if (rc) {
      ecmdOutputError("putvpdimage - Problems occurred parsing input data, must be an invalid format\n");
      return rc;
    }
  }
  
  /************************************************************************/
  /* Kickoff Looping Stuff                                                */
  /************************************************************************/
  rc = ecmdLooperInit(target, ECMD_SELECTED_TARGETS_LOOP, looperdata);
  if (rc) return rc;
  
  while (ecmdLooperNext(target, looperdata) && (!coeRc || coeMode)) {

    if (!strcasecmp(vpdType, "MOD")) {
      rc = putModuleVpdImage(target, data);
    } else {
      rc = putFruVpdImage(target, data);
    }
    if (rc) {
      printed = "putvpdimage - Error occurred performing putModuleVpdImage ";
      printed += (!strcasecmp(vpdType, "MOD") ? "putModuleVpdImage" : "putFruVpdImage");
      printed += " on " + ecmdWriteTarget(target) + "\n";
      ecmdOutputError( printed.c_str() );
      coeRc = rc;
      continue;
      //return rc;
    }
    else {
      validPosFound = true;     
    }

    if (!ecmdGetGlobalVar(ECMD_GLOBALVAR_QUIETMODE)) {
      printed = ecmdWriteTarget(target) + "\n";
      ecmdOutput(printed.c_str());
    }
  }
  // coeRc will be the return code from in the loop, coe mode or not.
  if (coeRc) return coeRc;

  // This is an error common across all UI functions
  if (!validPosFound) {
    ecmdOutputError("putvpdimage - Unable to find a valid chip to execute command on\n");
    return ECMD_TARGET_NOT_CONFIGURED;
  }

  return rc;
}

uint32_t ecmdGetVpdImageUser(int argc, char * argv[]) {

  uint32_t rc = ECMD_SUCCESS , coeRc = ECMD_SUCCESS;
  ecmdLooperData looperdata;            ///< Store internal Looper data
  std::string outputformat = "xl";      ///< Output Format to display
  ecmdChipTarget target;                ///< Current target operating on
  ecmdDataBuffer data;                  ///< Data from the module vpd record
  bool validPosFound = false;           ///< Did the looper find anything to execute on
  bool outputformatflag = false;
  std::string newFilename;              ///< filename with target postfix incase of multi positions
  ecmdChipTarget target1;               ///< Current target operating on-for second looper
  ecmdLooperData looperdata1;           ///< looper to do the real work
  char targetStr[50];                   ///< target postfix for the filename incase of multi positions
  int targetCount=0;                    ///< counts the number of targets user specified
  
  /************************************************************************/
  /* Parse Local FLAGS here!                                              */
  /************************************************************************/
  
  /* get format flag, if it's there */
  char * formatPtr = ecmdParseOptionWithArgs(&argc, &argv, "-o");
  if (formatPtr != NULL) {
    outputformat = formatPtr;
    outputformatflag = true;
  }
  
  /* Get the filename if -fb is specified */
  char * filename = ecmdParseOptionWithArgs(&argc, &argv, "-fb");
  std::string printed;

  if((filename != NULL) && (outputformatflag) ) {
    printed = "getvpdimage - Options '-fb' cannot be specified with '-o' option.\n";
    ecmdOutputError(printed.c_str());
    return ECMD_INVALID_ARGS;
  } 
  /************************************************************************/
  /* Parse Common Cmdline Args                                            */
  /************************************************************************/

  rc = ecmdCommandArgs(&argc, &argv);
  if (rc) return rc;

  /* Global args have been parsed, we can read if -coe was given */
  bool coeMode = ecmdGetGlobalVar(ECMD_GLOBALVAR_COEMODE); ///< Are we in continue on error mode
  /************************************************************************/
  /* Parse Local ARGS here!                                               */
  /************************************************************************/
  if (argc < 3) {  
    ecmdOutputError("getvpdimage - Too few arguments specified; you need at least the chip, vpdtype and numbytes.\n");
    ecmdOutputError("getvpdimage - Type 'getvpdimage -h' for usage.\n");
    return ECMD_INVALID_ARGS;
  } else if (argc > 3) {
    ecmdOutputError("getvpdimage - Too many arguments specified; you only need chip, vpdtype and numbytes.\n");
    ecmdOutputError("getvpdimage - Type 'getvpdimage -h' for usage.\n");
    return ECMD_INVALID_ARGS;
  }

  //Setup the target that will be used to query the system config 
  std::string chipType, chipUnitType;
  ecmdParseChipField(argv[0], chipType, chipUnitType);
  if (chipUnitType != "") {
    ecmdOutputError("getvpdkeyword - chipUnit specified on the command line, this function doesn't support chipUnits.\n");
    return ECMD_INVALID_ARGS;
  }
  target.chipType = chipType;
  if (target.chipType == "nochip") {
    target.chipTypeState = ECMD_TARGET_FIELD_UNUSED;
    target.posState = ECMD_TARGET_FIELD_UNUSED;
  } else {
    target.chipTypeState = ECMD_TARGET_FIELD_VALID;
    target.posState = ECMD_TARGET_FIELD_WILDCARD;
  }
  target.cageState = target.nodeState = target.slotState = ECMD_TARGET_FIELD_WILDCARD;
  target.chipUnitTypeState = target.chipUnitNumState = target.threadState = ECMD_TARGET_FIELD_UNUSED;

  target1 = target; //Created for the second looper needed for -fb case with multiple positions

  char *vpdType = argv[1];
  if (!strcasecmp(vpdType, "MOD") && !strcasecmp(vpdType, "FRU")) {
    ecmdOutputError("getvpdimage - You have to specify either mod or fru for the vpd type\n");
    return ECMD_INVALID_ARGS;
  }

  if (!ecmdIsAllDecimal(argv[2])) {
    ecmdOutputError("getvpdimage - Non-decimal numbers detected in numBytes field\n");
    return ECMD_INVALID_ARGS;
  }
  
  uint32_t numBytes = (uint32_t)atoi(argv[2]);
  
  //Run the loop to Check the number of targets
  if (filename != NULL) {
    rc = ecmdLooperInit(target, ECMD_SELECTED_TARGETS_LOOP, looperdata);
    if (rc) return rc;
 
    while ( ecmdLooperNext(target, looperdata) ) {
      targetCount++;
    }
  }
  
  //Looper to do the actual work
  rc = ecmdLooperInit(target1, ECMD_SELECTED_TARGETS_LOOP, looperdata1);
  if (rc) return rc;
  
  while (ecmdLooperNext(target1, looperdata1) && (!coeRc || coeMode)) {
    if (!strcasecmp(vpdType, "MOD")) {
      rc = getModuleVpdImage(target1, numBytes, data);
    } else {
      rc = getFruVpdImage(target1, numBytes, data);
    }
    if (rc) {
      printed = "getvpdimage - Error occurred performing getModuleVpdImage on ";
      printed += (!strcasecmp(vpdType, "MOD") ? "getModuleVpdImage" : "getFruVpdImage");
      printed += " on " + ecmdWriteTarget(target1) + "\n";
      ecmdOutputError( printed.c_str() );
      coeRc = rc;
      continue;
      //return rc;

    }
    else {
      validPosFound = true;     
    }


    printed = ecmdWriteTarget(target1) + "\n";
    if (filename != NULL) {

      if (targetCount > 1) {
        sprintf(targetStr, "k%dn%ds%dp%d", target1.cage, target1.node, target1.slot, target1.pos); 
        newFilename = (std::string)filename+"."+(std::string)targetStr;
      }
      else { newFilename = (std::string)filename; }

      rc = data.writeFile(newFilename.c_str(), ECMD_SAVE_FORMAT_BINARY_DATA);

      if (rc) {
        printed += "getvpdimage - Problems occurred writing data into file " + newFilename + "\n";
        ecmdOutputError(printed.c_str()); 
        return rc;
      }
      ecmdOutput( printed.c_str() );

    } 
    else {
      std::string dataStr = ecmdWriteDataFormatted(data, outputformat);
      printed += dataStr;
      ecmdOutput( printed.c_str() );
    } 
  }
  // coeRc will be the return code from in the loop, coe mode or not.
  if (coeRc) return coeRc;

  // This is an error common across all UI functions
  if (!validPosFound) {
    ecmdOutputError("getvpdimage - Unable to find a valid chip to execute command on\n");
    return ECMD_TARGET_NOT_CONFIGURED;
  }

  return rc;
}
#endif // ECMD_REMOVE_VPD_FUNCTIONS

