/*=========================================================================

  Project                 : pv-meshless
  Module                  : vtkH5PartReader.h
  Revision of last commit : $Rev: 884 $
  Author of last commit   : $Author: biddisco $
  Date of last commit     : $Date:: 2010-04-06 12:03:55 +0200 #$

  Copyright (C) CSCS - Swiss National Supercomputing Centre.
  You may use modify and and distribute this code freely providing
  1) This copyright notice appears on all copies of source code
  2) An acknowledgment appears with any substantial usage of the code
  3) If this code is contributed to any other open source project, it
  must not be reformatted such that the indentation, bracketing or
  overall style is modified significantly.

  This software is distributed WITHOUT ANY WARRANTY; without even the
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

=========================================================================*/
// For PARAVIEW_USE_MPI
#include "vtkPVConfig.h"
#ifdef PARAVIEW_USE_MPI
  #include "vtkMPI.h"
  #include "vtkMPIController.h"
  #include "vtkMPICommunicator.h"
#endif
#include "vtkDummyController.h"
//
#include "vtkH5PartReader.h"
//
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkStreamingDemandDrivenPipeline.h"
#include "vtkDataArraySelection.h"
#include "vtkPointData.h"
#include "vtkPoints.h"
#include "vtkPolyData.h"
#include "vtkDataArray.h"
//
#include "vtkCharArray.h"
#include "vtkUnsignedCharArray.h"
#include "vtkShortArray.h"
#include "vtkUnsignedShortArray.h"
#include "vtkLongArray.h"
#include "vtkUnsignedLongArray.h"
#include "vtkLongLongArray.h"
#include "vtkUnsignedLongLongArray.h"
#include "vtkIntArray.h"
#include "vtkUnsignedIntArray.h"
#include "vtkFloatArray.h"
#include "vtkDoubleArray.h"
#include "vtkCellArray.h"
#include "vtkOutlineSource.h"
#include "vtkAppendPolyData.h"
#include "vtkBoundingBox.h"
//
#include <vtksys/SystemTools.hxx>
#include <vtksys/RegularExpression.hxx>
#include <vector>
//
#include "vtkCharArray.h"
#include "vtkUnsignedCharArray.h"
#include "vtkCharArray.h"
#include "vtkShortArray.h"
#include "vtkUnsignedShortArray.h"
#include "vtkIntArray.h"
#include "vtkLongArray.h"
#include "vtkFloatArray.h"
#include "vtkDoubleArray.h"
#include "vtkSmartPointer.h"
#include "vtkExtentTranslator.h"
#include "vtkParticleBoxTreeBSP.h"
#include "vtkBoundingBox.h"
#include "vtkIdListCollection.h"
//
#include <functional>
#include <algorithm>
#include <numeric>
#include "H5Part.h"
//
#include "vtkBoundsExtentTranslator.h"
//
#include "Testing/TestUtils.h" // random class
//----------------------------------------------------------------------------
extern char H5PART_GROUPNAME_STEP[256];
//----------------------------------------------------------------------------
vtkCxxSetObjectMacro(vtkH5PartReader, Controller, vtkMultiProcessController);
//----------------------------------------------------------------------------
/*!
  \ingroup h5part_utility

  This function can be used to query the Type of a dataset
  It is not used by the core H5Part library but is useful when
  reading generic data from the file.
  An example of usage would be (H5Tequal(datatype,H5T_NATIVE_FLOAT))
  any NATIVE type can be used to test.

  \return  \c an hdf5 handle to the native type of the data
*/
static hid_t H5PartGetNativeDatasetType(H5PartFile *f, const char *name)
{
  hid_t dataset, datatype, datatypen;
  if (!f->timegroup)
    {
    H5PartSetStep(f,f->timestep); /* choose current step */
    }
#if (!H5_USE_16_API && ((H5_VERS_MAJOR>1)||((H5_VERS_MAJOR==1)&&(H5_VERS_MINOR>=8))))
  dataset=H5Dopen(f->timegroup, name, H5P_DEFAULT);
#else
  dataset=H5Dopen(f->timegroup, name);
#endif
  datatype  = H5Dget_type(dataset);
  datatypen = H5Tget_native_type(datatype, H5T_DIR_DEFAULT);
  H5Tclose(datatype);
  H5Dclose(dataset);
  return datatypen;
}

//----------------------------------------------------------------------------
hid_t H5PartGetDiskShape(H5PartFile *f, hid_t dataset)
{
  hid_t space = H5Dget_space(dataset);
  if (H5PartHasView(f))
    {
    int r;
    hsize_t stride, count;
    hsize_t range[2];
    /* so, is this selection inclusive or exclusive? */
    range[0]=f->viewstart;
    range[1]=f->viewend;
    count = range[1]-range[0]; /* to be inclusive */
    stride=1;
    /* now we select a subset */
    if (f->diskshape>0)
      {
      r=H5Sselect_hyperslab(f->diskshape,H5S_SELECT_SET,
        range/* only using first element */,
        &stride,&count,NULL);
      }
    /* now we select a subset */
    r=H5Sselect_hyperslab(space,H5S_SELECT_SET,
      range,&stride,&count,NULL);
    if (r<0)
      {
      fprintf(stderr,"Abort: Selection Failed!\n");
      return space;
      }
    }
  return space;
}
//----------------------------------------------------------------------------
//#define JB_DEBUG__
#ifdef JB_DEBUG__
  #define OUTPUTTEXT(a) std::cout << (a) << std::endl; std::cout.flush();

    #undef vtkDebugMacro
    #define vtkDebugMacro(a)  \
    { \
      if (this->UpdatePiece>=0) { \
        vtkOStreamWrapper::EndlType endl; \
        vtkOStreamWrapper::UseEndl(endl); \
        vtkOStrStreamWrapper vtkmsg; \
        vtkmsg << "P(" << this->UpdatePiece << "): " a << "\n"; \
        OUTPUTTEXT(vtkmsg.str()); \
        vtkmsg.rdbuf()->freeze(0); \
      } \
    }

  #undef vtkErrorMacro
  #define vtkErrorMacro(a) vtkDebugMacro(a)
#endif
//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkH5PartReader);
//----------------------------------------------------------------------------
vtkH5PartReader::vtkH5PartReader()
{
  this->SetNumberOfInputPorts(0);
  //
  this->NumberOfTimeSteps               = 0;
  this->TimeStep                        = 0;
  this->ActualTimeStep                  = 0;
  this->TimeStepTolerance               = 1E-6;
  this->CombineVectorComponents         = 1;
  this->UseStridedMultiComponentRead    = 0;
  this->MultiComponentArraysAsFieldData = 0;
  this->GenerateVertexCells             = 0;
  this->FileName                        = NULL;
  this->H5FileId                        = NULL;
  this->Xarray                          = NULL;
  this->Yarray                          = NULL;
  this->Zarray                          = NULL;
  this->StepName                        = NULL;
  this->UpdatePiece                     = 0;
  this->UpdateNumPieces                 = 0;
  this->TimeOutOfRange                  = 0;
  this->MaskOutOfTimeRangeOutput        = 0;
  this->IntegerTimeStepValues           = 0;
  this->IgnorePartitionBoxes            = 0;
  this->DisplayPartitionBoxes           = 0;
  this->DisplayPieceBoxes               = 0;
  this->UseLinearBoxPartitioning        = 1;
  this->RandomizePartitionExtents       = 0;
  this->PointDataArraySelection         = vtkDataArraySelection::New();
  this->ExtentTranslator                = vtkBoundsExtentTranslator::New();
  this->SetXarray("Coords_0");
  this->SetYarray("Coords_1");
  this->SetZarray("Coords_2");
  this->Controller = NULL;
  this->SetController(vtkMultiProcessController::GetGlobalController());
  if (this->Controller == NULL) {
    this->SetController(vtkSmartPointer<vtkDummyController>::New());
  }
}
//----------------------------------------------------------------------------
vtkH5PartReader::~vtkH5PartReader()
{
  this->CloseFile();
  delete [] this->FileName;
  this->FileName = NULL;

  delete [] this->Xarray;
  this->Xarray = NULL;

  delete [] this->Yarray;
  this->Yarray = NULL;

  delete [] this->Zarray;
  this->Zarray = NULL;

  delete [] this->StepName;
  this->StepName = NULL;

  this->PointDataArraySelection->FastDelete();
  this->PointDataArraySelection = 0;

  this->ExtentTranslator->FastDelete();

  this->SetController(NULL);
}
//----------------------------------------------------------------------------
bool vtkH5PartReader::HasStep(int Step)
{
  if (!this->OpenFile())
    {
    return false;
    }

  if (H5PartHasStep(this->H5FileId, Step))
    {
    return true;
    }
  return false;
}
//----------------------------------------------------------------------------
void vtkH5PartReader::SetFileName(char *filename)
{
  if (this->FileName == NULL && filename == NULL)
    {
    return;
    }
  if (this->FileName && filename && (!strcmp(this->FileName,filename)))
    {
    return;
    }
  delete [] this->FileName;
  this->FileName = NULL;

  if (filename)
    {
    this->FileName = vtksys::SystemTools::DuplicateString(filename);
    this->SetFileModified();
    }
  this->Modified();
}
//----------------------------------------------------------------------------
void vtkH5PartReader::SetFileModified()
{
  this->FileModifiedTime.Modified();
  this->Modified();
}
//----------------------------------------------------------------------------
void vtkH5PartReader::CloseFile()
{

  if (this->H5FileId != NULL)
    {
    H5PartCloseFile(this->H5FileId);
    this->H5FileId = NULL;
    }
}
//----------------------------------------------------------------------------
void vtkH5PartReader::CloseFileIntermediate()
{
}
//----------------------------------------------------------------------------
int vtkH5PartReader::OpenFile()
{
  if (this->StepName != NULL) {
      strcpy(H5PART_GROUPNAME_STEP, this->StepName);
  }
  if (!this->FileName)
    {
    vtkErrorMacro(<<"FileName must be specified.");
    return 0;
    }

  if (FileModifiedTime>FileOpenedTime)
    {
    this->CloseFile();
    }

  if (!this->H5FileId)
    {
    this->H5FileId = H5PartOpenFile(this->FileName, H5PART_READ);
    this->FileOpenedTime.Modified();
    }

  if (!this->H5FileId)
    {
    vtkErrorMacro(<< "Initialize: Could not open file " << this->FileName);
    return 0;
    }

  return 1;
}
//----------------------------------------------------------------------------
int vtkH5PartReader::IndexOfVectorComponent(const char *name)
{
  if (!this->CombineVectorComponents)
    {
    return 0;
    }
  //
  vtksys::RegularExpression re1(".*_([0-9]+)");
  if (re1.find(name))
    {
    int index = atoi(re1.match(1).c_str());
    return index+1;
    }
  return 0;
}
//----------------------------------------------------------------------------
std::string vtkH5PartReader::NameOfVectorComponent(const char *name)
{
  if (!this->CombineVectorComponents)
    {
    return name;
    }
  //
  vtksys::RegularExpression re1("(.*)_[0-9]+");
  if (re1.find(name))
    {
    return re1.match(1);
    }
  return name;
}
//----------------------------------------------------------------------------
int vtkH5PartReader::RequestInformation(
  vtkInformation *vtkNotUsed(request),
  vtkInformationVector **vtkNotUsed(inputVector),
  vtkInformationVector *outputVector)
{
  vtkInformation *outInfo = outputVector->GetInformationObject(0);
  //
  this->UpdatePiece = outInfo->Get(vtkStreamingDemandDrivenPipeline::UPDATE_PIECE_NUMBER());
  this->UpdateNumPieces = outInfo->Get(vtkStreamingDemandDrivenPipeline::UPDATE_NUMBER_OF_PIECES());
  //
  outInfo->Set(CAN_HANDLE_PIECE_REQUEST(), 1);
  bool NeedToReadInformation = (FileModifiedTime>FileOpenedTime || !this->H5FileId);

  if (NeedToReadInformation)
    {
    if (!this->OpenFile())
      {
      return 0;
      }

    this->NumberOfTimeSteps = H5PartGetNumSteps(this->H5FileId);
    H5PartSetStep(this->H5FileId, 0);
    int nds = H5PartGetNumDatasets(this->H5FileId);
    char name[512];
    for (int i=0; i<nds; i++)
      {
      // return 0 for no, 1,2,3,4,5 etc for index (1 based offset)
      H5PartGetDatasetName(this->H5FileId, i, name, 512);
      this->PointDataArraySelection->AddArray(name);
      }

    this->TimeStepValues.assign(this->NumberOfTimeSteps, 0.0);
    int validTimes = 0;
    for (int i=0; i<this->NumberOfTimeSteps; ++i)
      {
      H5PartSetStep(this->H5FileId, i);
      // Get the time value if it exists
      h5part_int64_t numAttribs = H5PartGetNumStepAttribs(this->H5FileId);
      if (numAttribs>0)
        {
        char           attribName[128];
        h5part_int64_t attribNameLength = 128;
        h5part_int64_t attribType       = 0;
        h5part_int64_t attribNelem      = 0;
        for (h5part_int64_t a=0; a<numAttribs; a++)
          {
          h5part_int64_t status = H5PartGetStepAttribInfo (
            this->H5FileId, a, attribName, attribNameLength,
            &attribType, &attribNelem);
          if (status==H5PART_SUCCESS && !strncmp("TimeValue",attribName,attribNameLength))
            {
            if (H5Tequal(attribType,H5T_NATIVE_DOUBLE) && attribNelem==1)
              {
              status=H5PartReadStepAttrib(this->H5FileId, attribName, &this->TimeStepValues[i]);
              if (status==H5PART_SUCCESS)
                {
                validTimes++;
                }
              }
            }
          }
        }
      }
    H5PartSetStep(this->H5FileId, 0);

    if (this->NumberOfTimeSteps==0)
      {
      vtkErrorMacro(<<"No time steps in data");
      return 0;
      }

    // if TIME information was either not present ot not consistent, then
    // set something so that consumers of this data can iterate sensibly
    if (this->IntegerTimeStepValues || (this->NumberOfTimeSteps>0 && this->NumberOfTimeSteps!=validTimes))
      {
      for (int i=0; i<this->NumberOfTimeSteps; i++)
        {
        // insert read of Time array here
        this->TimeStepValues[i] = i;
        }
      }
    outInfo->Set(vtkStreamingDemandDrivenPipeline::TIME_STEPS(),
      &this->TimeStepValues[0],
      static_cast<int>(this->TimeStepValues.size()));
    double timeRange[2];
    timeRange[0] = this->TimeStepValues.front();
    timeRange[1] = this->TimeStepValues.back();
    if (this->TimeStepValues.size()>1)
      {
      this->TimeStepTolerance = 0.01*(this->TimeStepValues[1]-this->TimeStepValues[0]);
      }
    else
      {
      this->TimeStepTolerance = 1E-3;
      }
    outInfo->Set(vtkStreamingDemandDrivenPipeline::TIME_RANGE(), timeRange, 2);

    //
    // If the file has bounding box partition support
    //
    vtkIdType partitions = this->IgnorePartitionBoxes ? 0 : this->ReadBoundingBoxes();
    if (partitions>0)
      {
      outInfo->Set(vtkBoundsExtentTranslator::META_DATA(), this->ExtentTranslator);
      }
    else {
      this->PartitionCount.clear();
      this->PartitionOffset.clear();
      this->PieceId.clear();
      this->PartitionBoundsTable.clear();
      this->PartitionBoundsTableHalo.clear();
    }
  }

  this->CloseFileIntermediate();

  return 1;
}
//----------------------------------------------------------------------------
int GetVTKDataType(int datatype)
{
  if (H5Tequal(datatype,H5T_NATIVE_FLOAT))
    {
    return VTK_FLOAT;
    }
  else if (H5Tequal(datatype,H5T_NATIVE_DOUBLE))
    {
    return VTK_DOUBLE;
    }
  else if (H5Tequal(datatype,H5T_NATIVE_SCHAR))
    {
    return VTK_CHAR;
    }
  else if (H5Tequal(datatype,H5T_NATIVE_UCHAR))
    {
    return VTK_UNSIGNED_CHAR;
    }
  else if (H5Tequal(datatype,H5T_NATIVE_SHORT))
    {
    return VTK_SHORT;
    }
  else if (H5Tequal(datatype,H5T_NATIVE_USHORT))
    {
    return VTK_UNSIGNED_SHORT;
    }
  else if (H5Tequal(datatype,H5T_NATIVE_INT))
    {
    return VTK_INT;
    }
  else if (H5Tequal(datatype,H5T_NATIVE_UINT))
    {
    return VTK_UNSIGNED_INT;
    }
  else if (H5Tequal(datatype,H5T_NATIVE_LONG))
    {
    return VTK_LONG;
    }
  else if (H5Tequal(datatype,H5T_NATIVE_ULONG))
    {
    return VTK_UNSIGNED_LONG;
    }
  else if (H5Tequal(datatype,H5T_NATIVE_LLONG))
    {
    return VTK_LONG_LONG;
    }
  else if (H5Tequal(datatype,H5T_NATIVE_ULLONG))
    {
    return VTK_UNSIGNED_LONG_LONG;
    }
  return VTK_VOID;
}

//----------------------------------------------------------------------------
template <class T1, class T2>
void CopyIntoTuple(int offset, vtkDataArray *source, vtkDataArray *dest)
{
  vtkIdType N = source->GetNumberOfTuples();
  T1 *sptr = static_cast<T1*>(source->GetVoidPointer(0));
  T2 *dptr = static_cast<T2*>(dest->WriteVoidPointer(0,N)) + offset;
  for (vtkIdType i=0; i<N; ++i) {
    *dptr = *sptr++;
    dptr += 3;
  }
}
//----------------------------------------------------------------------------
template <class T2>
void vtkH5PartReader::CopyIntoVector(int offset, vtkDataArray *source, vtkDataArray *dest)
{
  switch (source->GetDataType())
  {
    case VTK_CHAR:
    case VTK_SIGNED_CHAR:
    case VTK_UNSIGNED_CHAR:
      CopyIntoTuple<char,T2>(offset, source, dest);
      break;
    case VTK_SHORT:
      CopyIntoTuple<short int,T2>(offset, source, dest);
      break;
    case VTK_UNSIGNED_SHORT:
      CopyIntoTuple<unsigned short int,T2>(offset, source, dest);
      break;
    case VTK_INT:
      CopyIntoTuple<int,T2>(offset, source, dest);
      break;
    case VTK_UNSIGNED_INT:
      CopyIntoTuple<unsigned int,T2>(offset, source, dest);
      break;
    case VTK_LONG:
      CopyIntoTuple<long int,T2>(offset, source, dest);
      break;
    case VTK_UNSIGNED_LONG:
      CopyIntoTuple<unsigned long int,T2>(offset, source, dest);
      break;
    case VTK_LONG_LONG:
      CopyIntoTuple<long long,T2>(offset, source, dest);
      break;
    case VTK_UNSIGNED_LONG_LONG:
      CopyIntoTuple<unsigned long long,T2>(offset, source, dest);
      break;
    case VTK_FLOAT:
      CopyIntoTuple<float,T2>(offset, source, dest);
      break;
    case VTK_DOUBLE:
      CopyIntoTuple<double,T2>(offset, source, dest);
      break;
    case VTK_ID_TYPE:
      CopyIntoTuple<vtkIdType,T2>(offset, source, dest);
      break;
    default:
      break;
      vtkErrorMacro(<<"Unexpected data type");
  }
}
//----------------------------------------------------------------------------
/*
std::pair<double, double> GetClosest(std::vector<double> &sortedlist, const double& val) const
{
  std::vector<double>::const_iterator it = std::lower_bound(sortedlist.begin(), sortedlist.end(), val);
  if (it == sortedlist.end())        return std::make_pair(sortedlist.back(), sortedlist.back());
  else if (it == sortedlist.begin()) return std::make_pair(sortedlist.front(), sortedlist.front());
  else return std::make_pair(*(it - 1), *(it));
}
*/
class H5PartToleranceCheck: public std::binary_function<double, double, bool>
{
public:
  H5PartToleranceCheck(double tol) { this->tolerance = tol; }
  double tolerance;
  //
    result_type operator()(first_argument_type a, second_argument_type b) const
    {
      bool result = (fabs(a-b)<=(this->tolerance));
      return (result_type)result;
    }
};
//----------------------------------------------------------------------------
#if (!H5_USE_16_API && ((H5_VERS_MAJOR>1)||((H5_VERS_MAJOR==1)&&(H5_VERS_MINOR>=8))))
  #define h_params ,H5P_DEFAULT
#else
  #define h_params
#endif
//----------------------------------------------------------------------------
int vtkH5PartReader::RequestData(
  vtkInformation *vtkNotUsed(request),
  vtkInformationVector **vtkNotUsed(inputVector),
  vtkInformationVector *outputVector)
{
  vtkInformation *outInfo = outputVector->GetInformationObject(0);
  vtkPolyData     *output = vtkPolyData::SafeDownCast(outInfo->Get(vtkDataObject::DATA_OBJECT()));
  //
  this->UpdatePiece = outInfo->Get(vtkStreamingDemandDrivenPipeline::UPDATE_PIECE_NUMBER());
  this->UpdateNumPieces = outInfo->Get(vtkStreamingDemandDrivenPipeline::UPDATE_NUMBER_OF_PIECES());
  //
  typedef std::map< std::string, std::vector<std::string> > FieldMap;
  FieldMap scalarFields;
  //
  if (this->TimeStepValues.size()==0) return 0;
  //
  // Make sure that the user selected arrays for coordinates are represented
  //
  std::vector<std::string> coordarrays(3, "");
  //
  int N = this->PointDataArraySelection->GetNumberOfArrays();
  for (int i=0; i<N; i++)
    {
    const char *name = this->PointDataArraySelection->GetArrayName(i);
    // Do we want to load this array
    bool processarray = false;
    if (!vtksys::SystemTools::Strucmp(name,this->Xarray))
      {
      processarray = true;
      coordarrays[0] = name;
      }
    if (!vtksys::SystemTools::Strucmp(name,this->Yarray))
      {
      processarray = true;
      coordarrays[1] = name;
      }
    if (!vtksys::SystemTools::Strucmp(name,this->Zarray))
      {
      processarray = true;
      coordarrays[2] = name;
      }
    if (this->PointDataArraySelection->ArrayIsEnabled(name))
      {
      processarray = true;
      }
    if (!processarray)
      {
      continue;
      }

    // make sure we cater for multi-component vector fields
    int vectorcomponent;
    if ((vectorcomponent=this->IndexOfVectorComponent(name))>0)
      {
      std::string vectorname = this->NameOfVectorComponent(name) + "_v";
      FieldMap::iterator pos = scalarFields.find(vectorname);
      if (pos==scalarFields.end())
        {
        std::vector<std::string> arraylist(1, name);
        FieldMap::value_type element(vectorname, arraylist);
        scalarFields.insert(element);
        }
      else
        {
        pos->second.reserve(vectorcomponent);
        pos->second.resize(std::max((int)(pos->second.size()), vectorcomponent));
        pos->second[vectorcomponent-1] = name;
        }
      }
    else
      {
      std::vector<std::string> arraylist(1, name);
      FieldMap::value_type element(name, arraylist);
      scalarFields.insert(element);
      }
    }
  //
  FieldMap::iterator coordvector=scalarFields.end();
  for (FieldMap::iterator pos=scalarFields.begin(); pos!=scalarFields.end(); ++pos)
    {
    if (pos->second.size()==3 &&
      (pos->second[0]==coordarrays[0]) &&
      (pos->second[1]==coordarrays[1]) &&
      (pos->second[2]==coordarrays[2]))
      {
      // change the keyname of this entry to "coords" to ensure we use it as such
      FieldMap::value_type element("Coords", pos->second);
      scalarFields.erase(pos);
      coordvector = scalarFields.insert(element).first;
      break;
      }
    }

  if (coordvector==scalarFields.end())
    {
    FieldMap::value_type element("Coords", coordarrays);
    scalarFields.insert(element);
    }

  if (!this->MultiComponentArraysAsFieldData) {
    FieldMap::iterator posx=scalarFields.find(coordarrays[0]);
    if (posx!=scalarFields.end()) scalarFields.erase(posx);
    FieldMap::iterator posy=scalarFields.find(coordarrays[1]);
    if (posy!=scalarFields.end()) scalarFields.erase(posy);
    FieldMap::iterator posz=scalarFields.find(coordarrays[2]);
    if (posz!=scalarFields.end()) scalarFields.erase(posz);
  }
  //
  // Get the TimeStep Requested from the information if present
  //
  this->TimeOutOfRange = 0;
  this->ActualTimeStep = this->TimeStep;
  if (outInfo->Has(vtkStreamingDemandDrivenPipeline::UPDATE_TIME_STEP()))
    {
    double requestedTimeValue = outInfo->Get(vtkStreamingDemandDrivenPipeline::UPDATE_TIME_STEP());
    this->ActualTimeStep = std::find_if(
      this->TimeStepValues.begin(), this->TimeStepValues.end(),
      std::bind2nd( H5PartToleranceCheck(
          this->IntegerTimeStepValues ? 0.5 : this->TimeStepTolerance ), requestedTimeValue ))
      - this->TimeStepValues.begin();
    //
    if (requestedTimeValue<this->TimeStepValues.front() || requestedTimeValue>this->TimeStepValues.back())
      {
      this->TimeOutOfRange = 1;
      }
    output->GetInformation()->Set(vtkDataObject::DATA_TIME_STEP(), requestedTimeValue);
    }
  else
    {
    double timevalue[1];
    unsigned int index = this->ActualTimeStep;
    if (index<this->TimeStepValues.size())
      {
      timevalue[0] = this->TimeStepValues[index];
      }
    else
      {
      timevalue[0] = this->TimeStepValues[0];
      }
    output->GetInformation()->Set(vtkDataObject::DATA_TIME_STEP(), timevalue[0]);
    }

  if (this->TimeOutOfRange && this->MaskOutOfTimeRangeOutput)
    {
    // don't do anything, just return success
    return 1;
    }

  // open the file if not already done
  if (!this->OpenFile())
    {
    return 0;
    }

  // Set the TimeStep on the H5 file
  H5PartSetStep(this->H5FileId, this->ActualTimeStep);
  //
  // Get the number of particles for this timestep
  //
  vtkIdType Nparticles = H5PartGetNumParticles(this->H5FileId);

  //
  // Split particles up per process for parallel load
  //
  std::vector<vtkIdType> minIds, maxIds, Ids;
  vtkIdType ParticleStart;
  vtkIdType ParticleEnd;
  //
  if (this->PartitionCount.size()>0 && this->PartitionByBoundingBoxes(minIds,maxIds,this->PieceBounds,this->PieceBoundsHalo)) {
    ParticleStart = minIds[this->UpdatePiece];
    ParticleEnd   = maxIds[this->UpdatePiece];
    this->ExtentTranslator->SetBoundsHalosEnabled(1);
    this->ExtentTranslator->SetNumberOfPieces(this->PieceBounds.size());
    for (int i=0; i<this->PieceBounds.size(); i++) {
      double bounds[6];
      this->PieceBounds[i].GetBounds(bounds);
      this->ExtentTranslator->SetBoundsForPiece(i, bounds);
      this->PieceBoundsHalo[i].GetBounds(bounds);
      this->ExtentTranslator->SetBoundsHaloForPiece(i, bounds);
    }
    this->ExtentTranslator->InitWholeBounds();
  }
  else {
    if (this->RandomizePartitionExtents) {
      this->PartitionByExtentsRandomized(Nparticles, Ids);
    }
    else {
      this->PartitionByExtents(Nparticles, Ids);
    }
    ParticleStart = Ids[0];
    ParticleEnd   = Ids[1];
  }
  vtkIdType            Nt = ParticleEnd - ParticleStart + 1;
  //

  // Setup arrays for reading data
  vtkSmartPointer<vtkPoints>    points = vtkSmartPointer<vtkPoints>::New();
  vtkSmartPointer<vtkDataArray> coords = NULL;
  for (FieldMap::iterator it=scalarFields.begin(); it!=scalarFields.end(); it++)
    {
    // use the type of the first array for all if it is a vector field
    std::vector<std::string> &arraylist = (*it).second;
    const char *array_name = arraylist[0].c_str();
    std::string rootname = this->NameOfVectorComponent(array_name);
    int Nc = static_cast<int>(arraylist.size());
    //
    vtkSmartPointer<vtkDataArray> dataarray = NULL;
    hid_t datatype = H5PartGetNativeDatasetType(H5FileId,array_name);
    int vtk_datatype = GetVTKDataType(datatype);

    if (vtk_datatype == VTK_VOID)
      {
      H5Tclose(datatype);
      vtkErrorMacro("An unexpected data type was encountered");
      return 0;
      }

    dataarray.TakeReference(vtkDataArray::CreateDataArray(vtk_datatype));
    dataarray->SetNumberOfComponents(Nc);
    dataarray->SetNumberOfTuples(Nt);
    dataarray->SetName(rootname.c_str());

    // now read the data components.
    herr_t r;
    hsize_t count1_mem[] = { (hsize_t)(Nt*Nc) };
    hsize_t count2_mem[] = { (hsize_t)(Nt) };
    hsize_t offset_mem[] = { (hsize_t)(0) };
    hsize_t stride_mem[] = { (hsize_t)(Nc) };
    hsize_t count1_dsk[] = { (hsize_t)(Nt) };
    hsize_t offset_dsk[] = { (hsize_t)(ParticleStart) };
    hsize_t stride_dsk[] = { (hsize_t)(1) };
    //
    for (int c=0; c<Nc; c++)
      {
      const char *name = arraylist[c].c_str();
      hid_t dataset   = H5Dopen(H5FileId->timegroup, name h_params);
      hid_t diskshape = H5PartGetDiskShape(H5FileId,dataset);
      /* parallel read needs hyperslab for disk */
      r = H5Sselect_hyperslab(diskshape, H5S_SELECT_SET,
        offset_dsk, stride_dsk, count1_dsk, NULL);
      if (Nc==1 || this->UseStridedMultiComponentRead)
        {
        hid_t memspace = H5Screate_simple(1, count1_mem, NULL);
        hid_t component_datatype = H5PartGetNativeDatasetType(H5FileId, name);
        /* read x/y/z arrays into strided mem - use hyperslab */
        offset_mem[0] = c;
        r = H5Sselect_hyperslab(
          memspace, H5S_SELECT_SET,
          offset_mem, stride_mem, count2_mem, NULL);
        if (H5Tequal(component_datatype,datatype))
          {
          H5Dread(dataset, datatype, memspace,
            diskshape, H5P_DEFAULT, dataarray->GetVoidPointer(0));
          }
        else
          {
          // read data into a temporary array of the right type and then copy it
          // over to the "dataarray".
          // This can be optimized to create a single component array. But I
          // don't understand the stride/offset stuff too well to fix that.
          vtkDataArray* temparray =
            vtkDataArray::CreateDataArray(GetVTKDataType(component_datatype));
          temparray->SetNumberOfComponents(Nc);
          temparray->SetNumberOfTuples(Nt);
          r = H5Sselect_hyperslab(
            memspace, H5S_SELECT_SET,
            offset_mem, stride_mem, count2_mem, NULL);
          H5Dread(dataset, component_datatype, memspace,
            diskshape, H5P_DEFAULT, temparray->GetVoidPointer(0));
          dataarray->CopyComponent(c, temparray, c);
          temparray->FastDelete();
          }
        H5Sclose(memspace);
        H5Tclose(component_datatype);
        }
      else
        {
        vtkSmartPointer<vtkDataArray> onearray = NULL;
        onearray.TakeReference(vtkDataArray::CreateDataArray(vtk_datatype));
        onearray->SetNumberOfComponents(1);
        onearray->SetNumberOfTuples(Nt);
        onearray->SetName(name);
        offset_mem[0] = 0;
        count1_mem[0] = Nt;
        stride_mem[0] = 1;
        hid_t memspace = H5Screate_simple(1, count1_mem, NULL);
        hid_t component_datatype = H5PartGetNativeDatasetType(H5FileId, name);
        r = H5Sselect_hyperslab(
          memspace, H5S_SELECT_SET,
          offset_mem, stride_mem, count2_mem, NULL);
        if (H5Tequal(component_datatype,datatype))
          {
          H5Dread(dataset, datatype, memspace,
            diskshape, H5P_DEFAULT, onearray->GetVoidPointer(0));
          }
        else
          {
          vtkErrorMacro("H5Part : Unhandled type change condition")
          }
        switch (dataarray->GetDataType())
          {
          case VTK_FLOAT:
            this->CopyIntoVector<float>(c,onearray,dataarray);
            break;
          case VTK_DOUBLE:
            this->CopyIntoVector<double>(c,onearray,dataarray);
            break;
          case VTK_CHAR:
          case VTK_SIGNED_CHAR:
          case VTK_UNSIGNED_CHAR:
            this->CopyIntoVector<char>(c,onearray,dataarray);
            break;
          case VTK_SHORT:
            CopyIntoVector<short int>(c,onearray,dataarray);
            break;
          case VTK_UNSIGNED_SHORT:
            CopyIntoVector<unsigned short int>(c,onearray,dataarray);
            break;
          case VTK_INT:
            CopyIntoVector<int>(c,onearray,dataarray);
            break;
          case VTK_UNSIGNED_INT:
            CopyIntoVector<unsigned int>(c,onearray,dataarray);
            break;
          case VTK_LONG:
            CopyIntoVector<long int>(c,onearray,dataarray);
            break;
          case VTK_UNSIGNED_LONG:
            CopyIntoVector<unsigned long int>(c,onearray,dataarray);
            break;
          case VTK_LONG_LONG:
            CopyIntoVector<long long>(c,onearray,dataarray);
            break;
          case VTK_UNSIGNED_LONG_LONG:
            CopyIntoVector<unsigned long long>(c,onearray,dataarray);
            break;
          case VTK_ID_TYPE:
            CopyIntoVector<vtkIdType>(c,onearray,dataarray);
            break;
          default:
          vtkErrorMacro("H5Part : Unhandled vector type")
          }
        H5Sclose(memspace);
        H5Tclose(component_datatype);
        // if the array we read for the vector component is a field array
        // then skip reading it twice.
        if (this->MultiComponentArraysAsFieldData) {
          output->GetPointData()->AddArray(onearray);
          }
        }
      H5Sclose(diskshape);
      H5Dclose(dataset);
      }
    H5Tclose(datatype);
    //
    if (dataarray)
      {
      if ((*it).first=="Coords") {
        coords = dataarray;
        coords->SetName("Coordinates");
        }
      else
        {
        output->GetPointData()->AddArray(dataarray);
        if (!output->GetPointData()->GetScalars())
          {
          output->GetPointData()->SetActiveScalars(dataarray->GetName());
          }
        }
      }
    }

  //
  // generate cells
  //
  if (this->GenerateVertexCells)
    {
    vtkSmartPointer<vtkCellArray> vertices = vtkSmartPointer<vtkCellArray>::New();
    vtkIdType *cells = vertices->WritePointer(Nt, 2*Nt);
    for (vtkIdType i=0; i<Nt; ++i)
      {
      cells[2*i] = 1;
      cells[2*i+1] = i;
      }
    output->SetVerts(vertices);
    }
  //
  //
  //
  if (!this->IgnorePartitionBoxes && (this->DisplayPartitionBoxes || this->DisplayPieceBoxes)) {
    this->DisplayBoundingBoxes(coords, output, ParticleStart, ParticleEnd);
  }
  //
  //
  //
  points->SetData(coords);
  output->SetPoints(points);
  //
  //
  // only subclasses actually close the file.
  //
  this->CloseFileIntermediate();

  return 1;
}
//----------------------------------------------------------------------------
vtkIdType vtkH5PartReader::ReadBoundingBoxes()
{
  H5E_auto2_t  errfunc;
  void        *errdata;
  vtkIdType    partitions = 0;

  // Prevent HDF5 printing errors if the bboxes dataset doesn't exist, save error handler
  H5Eget_auto(H5E_DEFAULT, &errfunc, &errdata);
  // Replace with NULL error handler
  H5Eset_auto(H5E_DEFAULT, NULL, NULL);

  // @TODO, use group/name string passed into reader ...
#if (H5_VERS_MAJOR>1)||((H5_VERS_MAJOR==1)&&(H5_VERS_MINOR>=8))
  hid_t partitiongroup = H5Gopen(H5FileId->file, "Partition#0", H5P_DEFAULT );
	hid_t dataset_id = (partitiongroup>0) ? H5Dopen ( partitiongroup, "Box", H5P_DEFAULT ) : -1;
#else
  hid_t partitiongroup = H5Gopen(H5FileId->file, "Partition#0");
	hid_t dataset_id = (partitiongroup>0) ? H5Dopen ( partitiongroup, "Box") : -1;
#endif
  // Replace normal error handler
  H5Eset_auto(H5E_DEFAULT, errfunc, errdata);

  // if all was ok, go ahead and read the data
  if (partitiongroup>0 && dataset_id>0) {
    hid_t diskshape = H5PartGetDiskShape(H5FileId, dataset_id);
    hsize_t dims[2], maxdims[2];
    herr_t err = H5Sget_simple_extent_dims(diskshape, dims, maxdims);
    if (err!=1) vtkErrorMacro("Error in H5Part bounding box dimensions read");
    partitions = dims[0]/13;
    //
    std::vector<double> data(dims[0], 0.0);
    err = H5Dread(dataset_id, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, &data[0]);
    if (err<0) vtkErrorMacro("Error in H5Part bounding box data read");
    //
    // generate boxes, 2 per partition (6*2+1 = 13 vals per partition)
    // count, min{x,y,z}, max{x,y,z}, ghostmin{x,y,z}, ghostmax{x,y,z}
    //
    this->PartitionCount.resize(partitions);
    this->PartitionOffset.resize(partitions+1,0);
    this->PieceId.resize(partitions,0); // values set in repartitioning methods
    this->PartitionBoundsTable.resize(partitions*6);
    this->PartitionBoundsTableHalo.resize(partitions*6);
    //
    for (vtkIdType i=0; i<partitions; i++) {
      vtkIdType offset1 = i*6;
      vtkIdType offset2 = i*13;
      this->PartitionCount[i]      = static_cast<vtkIdType>(data[0+offset2]);
      this->PartitionBoundsTable[0+offset1] = data[1+offset2];
      this->PartitionBoundsTable[1+offset1] = data[4+offset2];
      this->PartitionBoundsTable[2+offset1] = data[2+offset2];
      this->PartitionBoundsTable[3+offset1] = data[5+offset2];
      this->PartitionBoundsTable[4+offset1] = data[3+offset2];
      this->PartitionBoundsTable[5+offset1] = data[6+offset2];
      //
      this->PartitionBoundsTableHalo[0+offset1] = data[1+6+offset2];
      this->PartitionBoundsTableHalo[1+offset1] = data[4+6+offset2];
      this->PartitionBoundsTableHalo[2+offset1] = data[2+6+offset2];
      this->PartitionBoundsTableHalo[3+offset1] = data[5+6+offset2];
      this->PartitionBoundsTableHalo[4+offset1] = data[3+6+offset2];
      this->PartitionBoundsTableHalo[5+offset1] = data[6+6+offset2];
    }
    std::partial_sum(this->PartitionCount.begin(), this->PartitionCount.end(), this->PartitionOffset.begin()+1);
    //
    // cleanup hdf5
    //
    H5Sclose(diskshape);
  }
  if (dataset_id>0) H5Dclose(dataset_id);
  if (partitiongroup>0) H5Gclose(partitiongroup);
  //
  return partitions;
}
//----------------------------------------------------------------------------
vtkIdType vtkH5PartReader::DisplayBoundingBoxes(vtkDataArray *coords, vtkPolyData *output, vtkIdType extent0, vtkIdType extent1)
{
  vtkIdType partitions = this->DisplayPartitionBoxes ? this->PartitionCount.size() : 0;
  vtkIdType pieces = this->DisplayPieceBoxes ? this->PieceBounds.size() : 0;
  vtkIdType newBoxes = partitions + pieces;
  //
  std::cout << "Creating boxes " << newBoxes << std::endl;
  if (newBoxes==0) return 0;
  //
  vtkIdType N1 = coords->GetNumberOfTuples();
  vtkIdType N2 = newBoxes*8*2;
  vtkIdType N3 = (N1+N2);
  //
  // We will add 3 new data arrays per point, PartitionId, PieceId and occupation(count)
  //
  vtkSmartPointer<vtkIdTypeArray> occupation = vtkSmartPointer<vtkIdTypeArray>::New();
  occupation->SetNumberOfTuples(N3);
  occupation->SetName("Occupation");
  vtkSmartPointer<vtkIdTypeArray> boxId = vtkSmartPointer<vtkIdTypeArray>::New();
  boxId->SetNumberOfTuples(N3);
  boxId->SetName("Partition");
  vtkSmartPointer<vtkIdTypeArray> piece = vtkSmartPointer<vtkIdTypeArray>::New();
  piece->SetNumberOfTuples(N3);
  piece->SetName("PieceId");
  //
  vtkIdType index = 0;
  vtkBoundingBox box;
  vtkSmartPointer<vtkAppendPolyData> polys = vtkSmartPointer<vtkAppendPolyData>::New();
  //
  // set scalars for each particle
  //
  for (vtkIdType i=0; i<this->PartitionCount.size(); i++) {
    vtkIdType numParticles = this->PartitionCount[i];
    vtkIdType pieceId = this->PieceId[i];
    if ((index+numParticles)>=extent0 && (index<=extent1)) {
      for (vtkIdType p=0; p<numParticles; p++) {
        if (index>=extent0 && index<=extent1) {
          boxId->SetValue(index-extent0, i);
          occupation->SetValue(index-extent0, numParticles);
          piece->SetValue(index-extent0, pieceId);
        }
        index++;
      }
    }
    else {
      index +=numParticles;
      continue;
    }
  }
  //
  // We haven't read all particles, so set index to last valid ID
  //
  index = (extent1-extent0)+1;
  //
  // generate boxes, 2 per partition (6*2+1 = 13 vals per partition)
  // count, min{x,y,z}, max{x,y,z}, ghostmin{x,y,z}, ghostmax{x,y,z}
  //
  for (vtkIdType i=0; i<partitions; i++) {
    vtkSmartPointer<vtkOutlineSource> cube1 = vtkSmartPointer<vtkOutlineSource>::New();
    cube1->SetBounds(&this->PartitionBoundsTable[i*6]);
    cube1->Update();
    polys->AddInputData(cube1->GetOutput());
    //
    vtkSmartPointer<vtkOutlineSource> cube2 = vtkSmartPointer<vtkOutlineSource>::New();
    cube2->SetBounds(&this->PartitionBoundsTableHalo[i*6]);
    cube2->Update();
    polys->AddInputData(cube2->GetOutput());
  }
  //
  // generate boxes, 2 per piece
  //
  for (vtkIdType i=0; i<pieces; i++) {
    vtkSmartPointer<vtkOutlineSource> cube1 = vtkSmartPointer<vtkOutlineSource>::New();
    double bbb[6];
    this->PieceBounds[i].GetBounds(bbb);
    cube1->SetBounds(bbb);
    cube1->Update();
    polys->AddInputData(cube1->GetOutput());
    //
    this->PieceBoundsHalo[i].GetBounds(bbb);
    vtkSmartPointer<vtkOutlineSource> cube2 = vtkSmartPointer<vtkOutlineSource>::New();
    cube2->SetBounds(bbb);
    cube2->Update();
    polys->AddInputData(cube2->GetOutput());
  }
  polys->Update();
  //
  // Add the generated box points to our original list
  //
  vtkPoints *points = polys->GetOutput()->GetPoints();
  coords->Resize(N3); // might allocate more memory than we need.
  coords->SetNumberOfTuples(N3); // set limits to correct count
  for (vtkIdType P=0; P<N2; P++) {
    coords->SetTuple(N1+P, points->GetPoint(P));
  }
  //
  // Copy lines to output, but add N1 to the point ID of each line vertex
  //
  output->SetLines(polys->GetOutput()->GetLines());
  vtkIdType L = output->GetLines()->GetNumberOfCells();
  vtkIdTypeArray *linedata = output->GetLines()->GetData();
  // lines stored as : {2, p1, p2}, {2, p1, p2}, ...
  for (vtkIdType B=0; B<L; B++) {
    linedata->SetValue(B*3+1, N1 + linedata->GetValue(B*3+1));
    linedata->SetValue(B*3+2, N1 + linedata->GetValue(B*3+2));
  }
  //
  // set scalars for line/box
  //
  for (vtkIdType i=0; i<partitions; i++) {
    vtkIdType numParticles = this->PartitionCount[i];
    vtkIdType pieceId = this->PieceId[i];
    for (vtkIdType p=0; p<8*2; p++) {
      boxId->SetValue(index, i);
      occupation->SetValue(index, numParticles);
      piece->SetValue(index, pieceId);
      index++;
    }
  }
  for (vtkIdType i=0; i<pieces; i++) {
    vtkIdType numParticles = 0;
    vtkIdType pieceId = i;
    for (vtkIdType p=0; p<8*2; p++) {
      boxId->SetValue(index, 0);
      occupation->SetValue(index, numParticles);
      piece->SetValue(index, pieceId);
      index++;
    }
  }
  //
  // The existing scalar array must be padded out for the new points we added
  //
  output->GetPointData()->CopyAllocate(output->GetPointData(),N3);
  for (vtkIdType i=0; i<N2; i++) {
    output->GetPointData()->NullPoint(N1+i);
  }
  //
  // And now add the new arrays (correct size already)
  //
  output->GetPointData()->AddArray(boxId);
  output->GetPointData()->AddArray(occupation);
  output->GetPointData()->AddArray(piece);

  return N2;
}

//----------------------------------------------------------------------------
int vtkH5PartReader::SplitExtent(int piece, int numPieces, vtkIdType *ext)
{
    int numPiecesInFirstHalf;
    unsigned long size[3];
    int splitAxis;
    vtkIdType mid;

    if (piece >= numPieces || piece < 0)
    {
        return 0;
    }

    // keep splitting until we have only one piece.
    // piece and numPieces will always be relative to the current ext.
    int cnt = 0;
    while (numPieces > 1)
    {
        // Get the dimensions for each axis.
        size[0] = ext[1]-ext[0];
        size[1] = ext[3]-ext[2];
        size[2] = ext[5]-ext[4];
        // choose the biggest axis
        if (size[2] >= size[1] && size[2] >= size[0] && size[2]/2 >= 1)
        {
            splitAxis = 2;
        }
        else if (size[1] >= size[0] && size[1]/2 >= 1)
        {
            splitAxis = 1;
        }
        else if (size[0]/2 >= 1)
        {
            splitAxis = 0;
        }
        else
        {
            // signal no more splits possible
            splitAxis = -1;
        }

        if (splitAxis == -1)
        {
            // can not split any more.
            if (piece == 0)
            {
                // just return the remaining piece
                numPieces = 1;
            }
            else
            {
                // the rest must be empty
                return 0;
            }
        }
        else
        {
            // split the chosen axis into two pieces.
            numPiecesInFirstHalf = (numPieces / 2);
            mid = size[splitAxis];
            mid = (mid *  numPiecesInFirstHalf) / numPieces + ext[splitAxis*2];
            if (piece < numPiecesInFirstHalf)
            {
                // piece is in the first half
                // set extent to the first half of the previous value.
                ext[splitAxis*2+1] = mid;
                // piece must adjust.
                numPieces = numPiecesInFirstHalf;
            }
            else
            {
                // piece is in the second half.
                // set the extent to be the second half. (two halves share points)
                ext[splitAxis*2] = mid;
                // piece must adjust
                numPieces = numPieces - numPiecesInFirstHalf;
                piece -= numPiecesInFirstHalf;
            }
        }
    } // end of while

    return 1;
}

//----------------------------------------------------------------------------
int vtkH5PartReader::PartitionByExtents(vtkIdType N, std::vector<vtkIdType> &startend)
{
  vtkIdType WholeExtent[6] = { 0, N, 0, 0, 0, 0 };
  this->SplitExtent(this->UpdatePiece, this->UpdateNumPieces, WholeExtent);
/*
  vtkExtentTranslator *extTran = vtkExtentTranslator::New();
  extTran->SetSplitModeToBlock();
#if !defined(LIMIT_PARTITIONS)
  extTran->SetNumberOfPieces(this->UpdateNumPieces);
#else
  extTran->SetNumberOfPieces(LIMIT_PARTITIONS);
#endif
  extTran->SetPiece(this->UpdatePiece);
  extTran->SetWholeExtent(WholeExtent);
  extTran->PieceToExtent();
  int PartitionExtents[6];
  extTran->GetExtent(PartitionExtents);
  extTran->FastDelete();
*/
  startend.push_back(WholeExtent[0]);
  startend.push_back(WholeExtent[1]-1);
  vtkDebugMacro(<< "PartitionByExtents (Translator) " << startend[0] << " : " << startend[1] << " = " << (startend[1]-startend[0]+1));
  return 1;
}
//----------------------------------------------------------------------------
int vtkH5PartReader::PartitionByExtentsRandomized(vtkIdType N, std::vector<vtkIdType> &startend)
{
  Random r(12345);
  vtkIdType partitionsize = N/this->UpdateNumPieces;
  vtkIdType rand_max = partitionsize/2;
  vtkIdType rand_half = partitionsize/4;
  //
  vtkIdType epstart = 0;
  vtkIdType epend = 0;
  vtkIdType pstart = 0;
  vtkIdType pend = 0;
  vtkIdType rnddev;
  for (vtkIdType i=0; i<this->UpdateNumPieces; i++) {
    epstart = i*partitionsize;
    epend   = (i+1)*partitionsize;
    rnddev  = (r.nextNumberInt()%rand_max) - rand_half;
    //
    pend    = epend + rnddev;

    // Don't go past last particle
    if (pend>=N) {
      pend = N-1;
    }
    // Force last process to read up to last particle
    if (i==(this->UpdateNumPieces-1)) {
      pend = N-1;
    }
    //
    if (i==this->UpdatePiece) {
      startend.push_back(pstart);
      startend.push_back(pend);
    }
    pstart = pend+1;
  }

  vtkDebugMacro(<< "PartitionByExtents (randomized) " << startend[0] << " : " << startend[1] << " = " << (startend[1]-startend[0]+1));
  return 1;
}
//----------------------------------------------------------------------------
unsigned int mylog2(unsigned int val) {
  unsigned int ret = -1;
  while (val != 0) {
    val >>= 1;
    ret++;
  }
  return ret;
}
//----------------------------------------------------------------------------
int vtkH5PartReader::PartitionByBoundingBoxes(
  std::vector<vtkIdType> &minIds,
  std::vector<vtkIdType> &maxIds,
  std::vector<vtkBoundingBox> &PieceBounds,
  std::vector<vtkBoundingBox> &PieceHaloBounds)
{
  if (this->IgnorePartitionBoxes) {
    return 0;
  }
  vtkIdType NP = this->PartitionCount.size();
  vtkIdType NR = this->UpdateNumPieces;
  vtkIdType  N = NP/NR;
  vtkIdType  D = NP % NR;
  //
  if (NR==1 || N<1 || D>0) {
//    std::cout << "H5Part partitioning : Box method unavailable " << NP << ":" << NR << " % " << D << std::endl;
    return 0;
  }
//  std::cout << "H5Part partitioning : Using " << N << " Boxes per rank " << std::endl;
  //
  PieceBounds.assign(NR,vtkBoundingBox());
  PieceHaloBounds.assign(NR,vtkBoundingBox());
  minIds.assign(NR,VTK_INT_MAX);
  maxIds.assign(NR,VTK_INT_MIN);
  if (this->UseLinearBoxPartitioning) {
    bool ok = true;
    for (vtkIdType i=0; i<NP; i++) {
      vtkIdType pieceId = (i/N);
      this->PieceId[i] = pieceId;
      minIds[pieceId] = std::min(this->PartitionOffset[i], minIds[pieceId]);
      maxIds[pieceId] = std::max(this->PartitionOffset[i+1]-1, maxIds[pieceId]);
      PieceBounds[pieceId].AddBounds(&this->PartitionBoundsTable[i*6]);
      PieceHaloBounds[pieceId].AddBounds(&this->PartitionBoundsTableHalo[i*6]);
      //
      if ((this->PartitionOffset[i]>=minIds[pieceId] && this->PartitionOffset[i]<=maxIds[pieceId]) ||
          (this->PartitionOffset[i+1]>=minIds[pieceId] && this->PartitionOffset[i+1]<=maxIds[pieceId])) {
//        ok = false;
      }
    }
    if (ok) {
      return 1;
    }
    vtkDebugMacro("Linear Partitioning failed : using BSP tree instead");
  }

  //
  // Create a dataset with 1 point per partition centre
  vtkSmartPointer<vtkPolyData> poly = vtkSmartPointer<vtkPolyData>::New();
  vtkSmartPointer<vtkPoints> boxcentres = vtkSmartPointer<vtkPoints>::New();
  boxcentres->SetNumberOfPoints(NP);
  vtkBoundingBox box;
  double centre[3];
  for (vtkIdType i=0; i<NP; i++) {
    box.SetBounds(&this->PartitionBoundsTable[i*6]);
    box.GetCenter(centre);
    boxcentres->SetPoint(i, centre);
  }
  vtkSmartPointer<vtkCellArray> boxcells = vtkSmartPointer<vtkCellArray>::New();
  vtkIdType *cells = boxcells->WritePointer(NP, 2*NP);
  for (vtkIdType i=0; i<NP; ++i) {
    cells[2*i] = 1;
    cells[2*i+1] = i;
  }
  poly->SetPoints(boxcentres);
  poly->SetVerts(boxcells);
  // Construct a BSP tree and set the bounding boxes to our partition extents
  vtkSmartPointer<vtkParticleBoxTreeBSP> tree = vtkSmartPointer<vtkParticleBoxTreeBSP>::New();
  vtkSmartPointer<vtkDoubleArray> bounds = vtkSmartPointer<vtkDoubleArray>::New();
  bounds->SetNumberOfComponents(6);
  bounds->SetNumberOfTuples(NP);
  bounds->SetVoidArray(&this->PartitionBoundsTable[0], this->PartitionBoundsTable.size(), 1); // we delete array
  tree->SetParticleBoundsArray(bounds);
  tree->SetNumberOfCellsPerNode(N);
  tree->SetMaxLevel(mylog2(NR));
  //
  tree->SetDataSet(poly);
  tree->SetLazyEvaluation(0);
  tree->BuildLocator();
  //
  vtkSmartPointer<vtkIdListCollection> info;
  info.TakeReference(tree->GetLeafNodeCellInformation());

  vtkIdList* list;
  vtkCollectionSimpleIterator adit;
  int L = 0, part=0;
  for (info->InitTraversal(adit); (list = info->GetNextIdList(adit));) {
    std::cout << "Entries for list " << L << " (" << list->GetNumberOfIds() << ")" << std::endl;
    for (vtkIdType i=0; i<list->GetNumberOfIds(); i++) {
      std::cout << list->GetId(i) << " ";
      if (this->UpdatePiece==0) {
        this->PieceId[part++] = L;
      }
    }
    std::cout << std::endl;
    L++;
  }
  //
  return 0;
}
//----------------------------------------------------------------------------
int vtkH5PartReader::GetCoordinateArrayStatus(const char* name)
{
  return this->PointDataArraySelection->ArrayIsEnabled(name);
}
//----------------------------------------------------------------------------
void vtkH5PartReader::SetCoordinateArrayStatus(const char* name, int status)
{
  if (status)
    {
    this->PointDataArraySelection->EnableArray(name);
    }
  else
    {
    this->PointDataArraySelection->DisableArray(name);
    }
}

//----------------------------------------------------------------------------
const char* vtkH5PartReader::GetPointArrayName(int index)
{
  return this->PointDataArraySelection->GetArrayName(index);
}
//----------------------------------------------------------------------------
int vtkH5PartReader::GetPointArrayStatus(const char* name)
{
  return this->PointDataArraySelection->ArrayIsEnabled(name);
}
//----------------------------------------------------------------------------
void vtkH5PartReader::SetPointArrayStatus(const char* name, int status)
{
  if (status!=this->GetPointArrayStatus(name))
    {
    if (status)
      {
      this->PointDataArraySelection->EnableArray(name);
      }
    else
      {
      this->PointDataArraySelection->DisableArray(name);
      }
    this->Modified();
    }
}
//----------------------------------------------------------------------------
void vtkH5PartReader::Enable(const char* name)
{
  this->SetPointArrayStatus(name, 1);
}
//----------------------------------------------------------------------------
void vtkH5PartReader::Disable(const char* name)
{
  this->SetPointArrayStatus(name, 0);
}
//----------------------------------------------------------------------------
void vtkH5PartReader::EnableAll()
{
  this->PointDataArraySelection->EnableAllArrays();
}
//----------------------------------------------------------------------------
void vtkH5PartReader::DisableAll()
{
  this->PointDataArraySelection->DisableAllArrays();
}
//----------------------------------------------------------------------------
int vtkH5PartReader::GetNumberOfPointArrays()
{
  return this->PointDataArraySelection->GetNumberOfArrays();
}
//----------------------------------------------------------------------------
void vtkH5PartReader::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os,indent);

  os << indent << "FileName: " <<
    (this->FileName ? this->FileName : "(none)") << "\n";

  os << indent << "NumberOfSteps: " <<  this->NumberOfTimeSteps << "\n";
}
