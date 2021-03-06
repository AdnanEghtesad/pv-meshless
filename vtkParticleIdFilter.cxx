/*=========================================================================

  Program:   Visualization Toolkit
  Module:    vtkParticleIdFilter.cxx

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
#include "vtkParticleIdFilter.h"
//
#include "vtkCellData.h"
#include "vtkDataSet.h"
#include "vtkDataSet.h"
#include "vtkIdTypeArray.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkObjectFactory.h"
#include "vtkPointData.h"
#include "vtkSmartPointer.h"
//
#include "vtkDummyController.h"
//
#include <numeric> // for accumulate, partial_sum
//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkParticleIdFilter);
vtkCxxSetObjectMacro(vtkParticleIdFilter, Controller, vtkMultiProcessController);
//----------------------------------------------------------------------------
vtkParticleIdFilter::vtkParticleIdFilter()
{
  this->UpdatePiece     = 0;
  this->UpdateNumPieces = 1;
  this->Controller = NULL;
  this->SetController(vtkMultiProcessController::GetGlobalController());
  if (this->Controller == NULL) {
    this->SetController(vtkSmartPointer<vtkDummyController>::New());
  }
}
//----------------------------------------------------------------------------
vtkParticleIdFilter::~vtkParticleIdFilter()
{
  this->SetController(NULL);
}
//----------------------------------------------------------------------------
// 
// Map ids into attribute data
//
int vtkParticleIdFilter::RequestData(
  vtkInformation *vtkNotUsed(request),
  vtkInformationVector **inputVector,
  vtkInformationVector *outputVector)
{
  // get the info objects
  vtkInformation *inInfo = inputVector[0]->GetInformationObject(0);
  vtkInformation *outInfo = outputVector->GetInformationObject(0);

  // get the input and output
  vtkDataSet *input = vtkDataSet::SafeDownCast(
    inInfo->Get(vtkDataObject::DATA_OBJECT()));
  vtkDataSet *output = vtkDataSet::SafeDownCast(
    outInfo->Get(vtkDataObject::DATA_OBJECT()));

  vtkIdType numPts, numCells, id;
  vtkIdTypeArray *ptIds;
  vtkIdTypeArray *cellIds;
  vtkPointData *inPD=input->GetPointData(), *outPD=output->GetPointData();
  vtkCellData *inCD=input->GetCellData(), *outCD=output->GetCellData();

  //
  // Initialize
  //
  vtkDebugMacro(<<"Generating ids!");

  // First, copy the input to the output as a starting point
  output->CopyStructure( input );
  numPts = input->GetNumberOfPoints();
  numCells = input->GetNumberOfCells();

//  if (this->FieldData) {
    outPD->ShallowCopy(inPD);
    outCD->ShallowCopy(inCD);
//  }

  //
  // setup communicator for parallel work
  //
  if (this->Controller) {
    this->UpdatePiece     = this->Controller->GetLocalProcessId();
    this->UpdateNumPieces = this->Controller->GetNumberOfProcesses();
  }
  else {
    this->UpdatePiece     = 0;
    this->UpdateNumPieces = 1;
  }

  // Loop over points (if requested) and generate ids
  //
  if ( this->PointIds )
    {
    std::vector<int> PartialSum(this->UpdateNumPieces+1);
    if (this->Controller) {
      std::vector<vtkIdType> PointsPerProcess(this->UpdateNumPieces);
      this->Controller->AllGather(&numPts, &PointsPerProcess[0], 1);
      std::partial_sum(PointsPerProcess.begin(), PointsPerProcess.end(), PartialSum.begin()+1);
    }
    vtkIdType initialValue = PartialSum[this->UpdatePiece];
//    std::cout << "Id filter rank " << this->UpdatePiece << " Using offset " << initialValue << std::endl;
//    for (int i=0; i<PartialSum.size(); i++) {
//      std::cout << PartialSum[i] << " " ;
//    }
//      std::cout << std::endl;
    //
    ptIds = vtkIdTypeArray::New();
    ptIds->SetNumberOfValues(numPts);

    for (id=0; id < numPts; id++)
      {
      ptIds->SetValue(id, id+initialValue);
      }

    ptIds->SetName(this->IdsArrayName);
    int idx = outPD->AddArray(ptIds);
    outPD->SetActiveAttribute(idx, vtkDataSetAttributes::SCALARS);
    ptIds->FastDelete();
    }

  //
  // Loop over cells (if requested) and generate ids
  //
  if ( this->CellIds)
    {
    std::vector<int> PartialSum(this->UpdateNumPieces+1);
    if (this->Controller) {
      std::vector<vtkIdType> CellsPerProcess(this->UpdateNumPieces);
      this->Controller->AllGather(&numCells, &CellsPerProcess[0], 1);
      std::partial_sum(CellsPerProcess.begin(), CellsPerProcess.end(), PartialSum.begin()+1);  
    }
    vtkIdType initialValue = PartialSum[this->UpdatePiece];
    //
    cellIds = vtkIdTypeArray::New();
    cellIds->SetNumberOfValues(numCells);

    for (id=0; id < numCells; id++)
      {
      cellIds->SetValue(id, id+initialValue);
      }

    cellIds->SetName(this->IdsArrayName);
    if ( ! this->FieldData )
      {
      int idx = outCD->AddArray(cellIds);
      outCD->SetActiveAttribute(idx, vtkDataSetAttributes::SCALARS);
      outCD->CopyScalarsOff();
      }
    else
      {
      outCD->AddArray(cellIds);
      outCD->CopyFieldOff(this->IdsArrayName);
      }
    cellIds->FastDelete();
    }

  return 1;
}
//----------------------------------------------------------------------------
