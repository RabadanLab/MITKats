/*===================================================================

The Medical Imaging Interaction Toolkit (MITK)

Copyright (c) German Cancer Research Center,
Division of Medical and Biological Informatics.
All rights reserved.

This software is distributed WITHOUT ANY WARRANTY; without
even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE.

See LICENSE.txt or http://www.mitk.org for details.

===================================================================*/

#include "mitkThresholdMorphTool.h"
#include "mitkImage.h"
#include "mitkProperties.h"
#include "mitkToolManager.h"
// us
#include <usGetModuleContext.h>
#include <usModule.h>
#include <usModuleContext.h>
#include <usModuleResource.h>

namespace mitk
{
  MITK_TOOL_MACRO(MITKSEGMENTATION_EXPORT, ThresholdMorphTool, "ThresholdMorphTool");
}

mitk::ThresholdMorphTool::ThresholdMorphTool()
{
  m_PointSetNode = mitk::DataNode::New();
  m_PointSetNode->GetPropertyList()->SetProperty("name", mitk::StringProperty::New("ThresholdMorph_Seedpoint"));
  m_PointSetNode->GetPropertyList()->SetProperty("helper object", mitk::BoolProperty::New(true));
  m_PointSet = mitk::PointSet::New();
  m_PointSetNode->SetData(m_PointSet);
}

mitk::ThresholdMorphTool::~ThresholdMorphTool()
{
}

bool mitk::ThresholdMorphTool::CanHandle(BaseData *referenceData) const
{
  if (referenceData == NULL)
    return false;

  Image *image = dynamic_cast<Image *>(referenceData);

  if (image == NULL)
    return false;

  if (image->GetDimension() < 3)
    return false;

  return true;
}

const char **mitk::ThresholdMorphTool::GetXPM() const
{
  return NULL;
}

const char *mitk::ThresholdMorphTool::GetName() const
{
  return "Threshold Morph";
}

us::ModuleResource mitk::ThresholdMorphTool::GetIconResource() const
{
  us::Module *module = us::GetModuleContext()->GetModule();
  us::ModuleResource resource = module->GetResource("ThresholdMorph_48x48.png");
  return resource;
}
	
void mitk::ThresholdMorphTool::Activated()
{
  Superclass::Activated();

  if (!GetDataStorage()->Exists(m_PointSetNode))
    GetDataStorage()->Add(m_PointSetNode, GetWorkingData());
  m_SeedPointInteractor = mitk::SinglePointDataInteractor::New();
  m_SeedPointInteractor->LoadStateMachine("PointSet.xml");
  m_SeedPointInteractor->SetEventConfig("PointSetConfig.xml");
  m_SeedPointInteractor->SetDataNode(m_PointSetNode);
}

void mitk::ThresholdMorphTool::Deactivated()
{
  m_PointSet->Clear();
  GetDataStorage()->Remove(m_PointSetNode);

  Superclass::Deactivated();
}

void mitk::ThresholdMorphTool::ConfirmSegmentation()
{
  m_ToolManager->ActivateTool(-1);
}

mitk::DataNode *mitk::ThresholdMorphTool::GetReferenceData()
{
  return this->m_ToolManager->GetReferenceData(0);
}

mitk::DataStorage *mitk::ThresholdMorphTool::GetDataStorage()
{
  return this->m_ToolManager->GetDataStorage();
}

mitk::DataNode *mitk::ThresholdMorphTool::GetWorkingData()
{
  return this->m_ToolManager->GetWorkingData(0);
}

mitk::DataNode::Pointer mitk::ThresholdMorphTool::GetPointSetNode()
{
  return m_PointSetNode;
}
