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

#include "QmitkDeformableClippingPlaneView.h"

#include "mitkClippingPlaneInteractor3D.h"
#include "mitkHeightFieldSurfaceClipImageFilter.h"
#include <mitkILinkedRenderWindowPart.h>
#include "mitkImageToSurfaceFilter.h"
#include "mitkInteractionConst.h"
#include "mitkLabeledImageLookupTable.h"
#include "mitkLabeledImageVolumeCalculator.h"
#include "mitkLevelWindowProperty.h"
#include "mitkLookupTableProperty.h"
#include "mitkNodePredicateProperty.h"
#include "mitkNodePredicateDataType.h"
#include "mitkRenderingModeProperty.h"
#include "mitkRotationOperation.h"
#include "mitkSurfaceDeformationDataInteractor3D.h"
#include "mitkSurfaceVtkMapper3D.h"
#include "mitkVtkRepresentationProperty.h"
#include "usModuleRegistry.h"

#include "vtkFloatArray.h"
#include "vtkPointData.h"
#include "vtkProperty.h"
#include <vtkPlaneSource.h>

#include "mitkImageAccessByItk.h"
#include <itkBinaryThresholdImageFilter.h>
#include "mitkToolManagerProvider.h"
#include "mitkImageStatisticsHolder.h"
#include <itkImageToHistogramFilter.h>

const std::string QmitkDeformableClippingPlaneView::VIEW_ID = "org.mitk.views.deformableclippingplane";

QmitkDeformableClippingPlaneView::QmitkDeformableClippingPlaneView()
  : QmitkAbstractView()
  , m_ReferenceNode(nullptr)
  , m_WorkingNode(nullptr)
{
}

QmitkDeformableClippingPlaneView::~QmitkDeformableClippingPlaneView()
{
  if (m_WorkingNode.IsNotNull())
    m_WorkingNode->SetDataInteractor(nullptr);
}

void QmitkDeformableClippingPlaneView::CreateQtPartControl(QWidget *parent)
{
  m_Controls.setupUi(parent);

  auto isClippingPlane = mitk::NodePredicateProperty::New("clippingPlane", mitk::BoolProperty::New(true));

  m_Controls.clippingPlaneSelector->SetDataStorage(this->GetDataStorage());
  m_Controls.clippingPlaneSelector->SetPredicate(isClippingPlane);

  m_Controls.volumeGroupBox->setEnabled(false);
  m_Controls.interactionSelectionBox->setEnabled(false);
  m_Controls.noSelectedImageLabel->show();
  m_Controls.planesWarningLabel->hide();

  this->CreateConnections();

}

void QmitkDeformableClippingPlaneView::SetFocus()
{
  m_Controls.createNewPlanePushButton->setFocus();
}

void QmitkDeformableClippingPlaneView::CreateConnections()
{
  connect(m_Controls.translationPushButton, SIGNAL(toggled(bool)), this, SLOT(OnTranslationMode(bool)));
  connect(m_Controls.rotationPushButton, SIGNAL(toggled(bool)), this, SLOT(OnRotationMode(bool)));
  connect(m_Controls.deformationPushButton, SIGNAL(toggled(bool)), this, SLOT(OnDeformationMode(bool)));
  connect(m_Controls.createNewPlanePushButton, SIGNAL(clicked()), this, SLOT(OnCreateNewClippingPlane()));
  connect(m_Controls.updateVolumePushButton, SIGNAL(clicked()), this, SLOT(OnCalculateClippingVolume()));
  connect(m_Controls.clippingPlaneSelector, SIGNAL(OnSelectionChanged(const mitk::DataNode*)),
    this, SLOT(OnComboBoxSelectionChanged(const mitk::DataNode*)));
  connect(m_Controls.volumeList, SIGNAL(itemDoubleClicked(QListWidgetItem *)), this, SLOT(OnSelectClippedSegment(QListWidgetItem *)));

}

void QmitkDeformableClippingPlaneView::OnComboBoxSelectionChanged(const mitk::DataNode* node)
{
  this->DeactivateInteractionButtons();

  auto selectedNode = const_cast<mitk::DataNode*>(node);

  if(selectedNode != nullptr)
  {
    if(m_WorkingNode.IsNotNull())
      selectedNode->SetDataInteractor(m_WorkingNode->GetDataInteractor());

    m_WorkingNode = selectedNode;
  }

  this->UpdateView();
}

void QmitkDeformableClippingPlaneView::OnSelectionChanged(mitk::DataNode* node)
{
  berry::IWorkbenchPart::Pointer nullPart;
  QList<mitk::DataNode::Pointer> nodes;
  nodes.push_back(node);

  this->OnSelectionChanged(nullPart, nodes);
}

void QmitkDeformableClippingPlaneView::OnSelectionChanged(berry::IWorkbenchPart::Pointer /*part*/, const QList<mitk::DataNode::Pointer>& nodes)
{
  bool isClippingPlane = false;

  for (auto node : nodes)
  {
    node->GetBoolProperty("clippingPlane", isClippingPlane);

    if (isClippingPlane)
    {
      m_Controls.clippingPlaneSelector->setCurrentIndex(m_Controls.clippingPlaneSelector->Find(node));
    }
    else
    {
      if(node.IsNotNull() && dynamic_cast<mitk::Image*>(node->GetData()) != nullptr)
      {
        if(m_ReferenceNode.IsNotNull() && node->GetData() == m_ReferenceNode->GetData())
          return;

        m_ReferenceNode = node;
      }
    }
  }

  this->UpdateView();
}

void::QmitkDeformableClippingPlaneView::NodeChanged(const mitk::DataNode*)
{
  this->UpdateView();
}

void QmitkDeformableClippingPlaneView::NodeRemoved(const mitk::DataNode* node)
{
  bool isClippingPlane(false);
  node->GetBoolProperty("clippingPlane", isClippingPlane);

  if (isClippingPlane)
  {
    if (this->GetAllClippingPlanes()->Size() <= 1)
    {
      m_WorkingNode = nullptr;
      this->UpdateView();
    }
    else
    {
      this->OnSelectionChanged(this->GetAllClippingPlanes()->front() == node
        ? this->GetAllClippingPlanes()->ElementAt(1)
        : this->GetAllClippingPlanes()->front());
    }
  }
  else
  {
    if(m_ReferenceNode.IsNotNull())
    {
      if(node->GetData() == m_ReferenceNode->GetData())
      {
        m_ReferenceNode = nullptr;
        m_Controls.volumeList->clear();
      }

      this->UpdateView();
    }
  }
}

void QmitkDeformableClippingPlaneView::UpdateView()
{
  if (m_ReferenceNode.IsNotNull())
  {
    m_Controls.noSelectedImageLabel->hide();

    m_Controls.selectedImageLabel->setText(QString::fromUtf8(m_ReferenceNode->GetName().c_str()));

    if (m_WorkingNode.IsNotNull())
    {
      bool isSegmentation = false;
      m_ReferenceNode->GetBoolProperty("binary", isSegmentation);
      m_Controls.interactionSelectionBox->setEnabled(true);

      m_Controls.volumeGroupBox->setEnabled(isSegmentation);

      //clear list --> than search for all shown clipping plans (max 7 planes)
      m_Controls.selectedVolumePlanesLabel->setText("");
      m_Controls.planesWarningLabel->hide();
      int volumePlanes=0;

      mitk::DataStorage::SetOfObjects::ConstPointer allClippingPlanes = this->GetAllClippingPlanes();
      for (mitk::DataStorage::SetOfObjects::ConstIterator itPlanes = allClippingPlanes->Begin(); itPlanes != allClippingPlanes->End(); itPlanes++)
      {
        bool isVisible = false;
        itPlanes.Value()->GetBoolProperty("visible", isVisible);
        if (isVisible)
        {
          if (volumePlanes < 7)
          {
            ++volumePlanes;
            m_Controls.selectedVolumePlanesLabel->setText(m_Controls.selectedVolumePlanesLabel->text().append(QString::fromStdString(itPlanes.Value()->GetName()+"\n")));
          }
          else
          {
            m_Controls.planesWarningLabel->show();
            return;
          }
        }
      }
    }
    else
    {
      m_Controls.volumeGroupBox->setEnabled(false);
      m_Controls.interactionSelectionBox->setEnabled(false);
      m_Controls.selectedVolumePlanesLabel->setText("");
      m_Controls.volumeList->clear();
    }
  }
  else
  {
    m_Controls.volumeGroupBox->setEnabled(false);
    m_Controls.noSelectedImageLabel->show();
    m_Controls.selectedImageLabel->setText("");
    m_Controls.selectedVolumePlanesLabel->setText("");
    m_Controls.planesWarningLabel->hide();

    if (m_WorkingNode.IsNull())
    {
      m_Controls.interactionSelectionBox->setEnabled(false);
    }
    else
    {
      m_Controls.interactionSelectionBox->setEnabled(true);
    }
  }
}

void QmitkDeformableClippingPlaneView::OnCreateNewClippingPlane()
{
  this->DeactivateInteractionButtons();

  auto plane = mitk::Surface::New();
  auto referenceImage = mitk::Image::New();
  auto planeSource = vtkSmartPointer<vtkPlaneSource>::New();

  planeSource->SetOrigin(-32.0, -32.0, 0.0);
  planeSource->SetPoint1( 32.0, -32.0, 0.0);
  planeSource->SetPoint2(-32.0,  32.0, 0.0);
  planeSource->SetResolution(128, 128);
  planeSource->Update();

  plane->SetVtkPolyData(planeSource->GetOutput());

  double imageDiagonal = 200;

  if (m_ReferenceNode.IsNotNull())
  {
    referenceImage = dynamic_cast<mitk::Image*>(m_ReferenceNode->GetData());

    if (referenceImage.IsNotNull())
    {
      // check if user wants a surface model
      if(m_Controls.surfaceModelCheckBox->isChecked())
      {
        //Check if there is a surface node from the image. If not, create one
        bool createSurfaceFromImage(true);
        mitk::NodePredicateDataType::Pointer isDwi = mitk::NodePredicateDataType::New("DiffusionImage");
        mitk::NodePredicateDataType::Pointer isSurface = mitk::NodePredicateDataType::New("Surface");
        mitk::DataStorage::SetOfObjects::ConstPointer childNodes =  GetDataStorage()->GetDerivations(m_ReferenceNode,isSurface, true);

        for (mitk::DataStorage::SetOfObjects::ConstIterator itChildNodes = childNodes->Begin();
          itChildNodes != childNodes->End(); itChildNodes++)
        {
          if (itChildNodes.Value().IsNotNull() && itChildNodes->Value()->GetName().compare(m_ReferenceNode->GetName()) == 0)
          {
            createSurfaceFromImage = false;
            itChildNodes.Value()->SetVisibility(true);
          }
        }

        if(createSurfaceFromImage)
        {
          //Lsg 2: Surface for the 3D-perspective
          mitk::ImageToSurfaceFilter::Pointer surfaceFilter = mitk::ImageToSurfaceFilter::New();
          surfaceFilter->SetInput(referenceImage);
          surfaceFilter->SetThreshold(1);
          surfaceFilter->SetSmooth(true);
          //Downsampling
          surfaceFilter->SetDecimate(mitk::ImageToSurfaceFilter::DecimatePro);

          mitk::DataNode::Pointer surfaceNode = mitk::DataNode::New();
          surfaceNode->SetData(surfaceFilter->GetOutput());
          surfaceNode->SetProperty("color", m_ReferenceNode->GetProperty("color"));
          surfaceNode->SetOpacity(0.5);
          surfaceNode->SetName(m_ReferenceNode->GetName());
          GetDataStorage()->Add(surfaceNode, m_ReferenceNode);
        }
      }

      //If an image is selected trim the plane to this.
      imageDiagonal = referenceImage->GetGeometry()->GetDiagonalLength();
      plane->SetOrigin( referenceImage->GetGeometry()->GetCenter());

      // Rotate plane
      mitk::Vector3D rotationAxis;
      mitk::FillVector3D(rotationAxis, 0.0, 1.0, 0.0);
      mitk::RotationOperation op(mitk::OpROTATE, referenceImage->GetGeometry()->GetCenter(), rotationAxis, 90.0);
      plane->GetGeometry()->ExecuteOperation(&op);
    }
  }

  //set some properties for the clipping plane
  // plane->SetExtent(imageDiagonal * 0.9, imageDiagonal * 0.9);
  // plane->SetResolution(64, 64);

  // eequivalent to the extent and resolution function of the clipping plane
  const double x = imageDiagonal * 0.9;
  planeSource->SetOrigin( -x / 2.0, -x / 2.0, 0.0 );
  planeSource->SetPoint1(  x / 2.0, -x / 2.0, 0.0 );
  planeSource->SetPoint2( -x / 2.0,  x / 2.0, 0.0 );
  planeSource->SetResolution( 64, 64 );
  planeSource->Update();

  plane->SetVtkPolyData(planeSource->GetOutput());

  // Set scalars (for colorization of plane)
  vtkFloatArray *scalars = vtkFloatArray::New();
  scalars->SetName("Distance");
  scalars->SetNumberOfComponents(1);

  for ( unsigned int i = 0; i < plane->GetVtkPolyData(0)->GetNumberOfPoints(); ++i)
  {
    scalars->InsertNextValue(-1.0);
  }
  plane->GetVtkPolyData(0)->GetPointData()->SetScalars(scalars);
  plane->GetVtkPolyData(0)->GetPointData()->Update();

  mitk::DataNode::Pointer planeNode = mitk::DataNode::New();
  planeNode->SetData(plane);

  std::stringstream planeName;
  planeName << "ClippingPlane ";
  planeName << this->GetAllClippingPlanes()->Size() + 1;

  planeNode->SetName(planeName.str());
  planeNode->AddProperty("clippingPlane",mitk::BoolProperty::New(true));
  // Make plane pickable
  planeNode->SetBoolProperty("pickable", true);

  mitk::SurfaceVtkMapper3D::SetDefaultProperties(planeNode);

  // Don't include plane in bounding box!
  planeNode->SetProperty("includeInBoundingBox", mitk::BoolProperty::New(false));

  // Set lookup table for plane surface visualization
  vtkSmartPointer<vtkLookupTable> lookupTable = vtkSmartPointer<vtkLookupTable>::New();
  lookupTable->SetHueRange(0.6, 0.0);
  lookupTable->SetSaturationRange(1.0, 1.0);
  lookupTable->SetValueRange(1.0, 1.0);
  lookupTable->SetTableRange(-1.0, 1.0);
  lookupTable->Build();

  mitk::LookupTable::Pointer lut = mitk::LookupTable::New();
  lut->SetVtkLookupTable(lookupTable);

  mitk::LookupTableProperty::Pointer prop = mitk::LookupTableProperty::New(lut);

  planeNode->SetProperty("LookupTable", prop);
  planeNode->SetBoolProperty("scalar visibility", true);
  planeNode->SetBoolProperty("color mode", true);
  planeNode->SetFloatProperty("ScalarsRangeMinimum", -1.0);
  planeNode->SetFloatProperty("ScalarsRangeMaximum", 1.0);

  // Configure material so that only scalar colors are shown
  planeNode->SetColor(0.0f,0.0f,0.0f);
  planeNode->SetOpacity(1.0f);
  planeNode->SetFloatProperty("material.wireframeLineWidth",2.0f);

  //Set view of plane to wireframe
  planeNode->SetProperty("material.representation", mitk::VtkRepresentationProperty::New(VTK_WIREFRAME));

  //Set the plane as working data for the tools and selected it
  this->OnSelectionChanged (planeNode);

  //Add the plane to data storage
  this->GetDataStorage()->Add(planeNode);

  //Change the index of the selector to the new generated node
  m_Controls.clippingPlaneSelector->setCurrentIndex( m_Controls.clippingPlaneSelector->Find(planeNode) );

  m_Controls.interactionSelectionBox->setEnabled(true);

  if (auto renderWindowPart = dynamic_cast<mitk::ILinkedRenderWindowPart*>(this->GetRenderWindowPart()))
  {
    renderWindowPart->EnableSlicingPlanes(false);
  }

  mitk::RenderingManager::GetInstance()->RequestUpdateAll();
}

void QmitkDeformableClippingPlaneView::OnCalculateClippingVolume()
{
  bool isSegmentation(false);
  m_ReferenceNode->GetBoolProperty("binary", isSegmentation);

  if(m_ReferenceNode.IsNull() || !isSegmentation)
  {
    MITK_ERROR << "No segmentation selected! Can't calculate volume";
    return;
  }

  std::vector<mitk::Surface*> clippingPlanes;
  mitk::DataStorage::SetOfObjects::ConstPointer allClippingPlanes = this->GetAllClippingPlanes();
  for (mitk::DataStorage::SetOfObjects::ConstIterator itPlanes = allClippingPlanes->Begin(); itPlanes != allClippingPlanes->End(); itPlanes++)
  {
    bool isVisible(false);
    itPlanes.Value()->GetBoolProperty("visible",isVisible);
    mitk::Surface* plane = dynamic_cast<mitk::Surface*>(itPlanes.Value()->GetData());

    if (isVisible && plane)
      clippingPlanes.push_back(plane);
  }

  if (clippingPlanes.empty())
  {
    MITK_ERROR << "No clipping plane selected! Can't calculate volume";
    return;
  }

  // deactivate Tools
  this->DeactivateInteractionButtons();
  //Clear the list of volumes, before calculating the new values
  m_Controls.volumeList->clear();

  m_ReferenceNode->SetBoolProperty("visible", false);

  //set some properties for clipping the image-->Output: labled Image
  mitk::HeightFieldSurfaceClipImageFilter::Pointer surfaceClipFilter = mitk::HeightFieldSurfaceClipImageFilter::New();

  surfaceClipFilter->SetInput(dynamic_cast<mitk::Image*> (m_ReferenceNode->GetData()));
  surfaceClipFilter->SetClippingModeToMultiPlaneValue();
  surfaceClipFilter->SetClippingSurfaces(clippingPlanes);
  surfaceClipFilter->Update();

  //delete the old clipped image node
  mitk::DataStorage::SetOfObjects::ConstPointer oldClippedNode = this->GetDataStorage()->GetSubset(mitk::NodePredicateProperty::New("name", mitk::StringProperty::New("Clipped Image")));
  if (oldClippedNode.IsNotNull())
    this->GetDataStorage()->Remove(oldClippedNode);

  //add the new clipped image node
  mitk::DataNode::Pointer clippedNode = mitk::DataNode::New();
  mitk::Image::Pointer clippedImage = surfaceClipFilter->GetOutput();
  clippedImage->DisconnectPipeline();
  clippedNode->SetData(clippedImage);
  //clippedNode->SetProperty("helper object", mitk::BoolProperty::New(true));
  clippedNode->SetName("Clipped Image");
  clippedNode->SetColor(1.0,1.0,1.0);  // color property will not be used, labeled image lookuptable will be used instead
  clippedNode->SetProperty ("use color", mitk::BoolProperty::New(false));
  clippedNode->SetOpacity(0.4);
  this->GetDataStorage()->Add(clippedNode);

  mitk::LabeledImageVolumeCalculator::Pointer volumeCalculator = mitk::LabeledImageVolumeCalculator::New();
  volumeCalculator->SetImage(clippedImage);
  volumeCalculator->Calculate();

  std::vector<double> volumes = volumeCalculator->GetVolumes();

  mitk::LabeledImageLookupTable::Pointer lut = mitk::LabeledImageLookupTable::New();
  int lablesWithVolume=0;

  for(unsigned int i = 1; i < volumes.size(); ++i)
  {
    if(volumes.at(i)!=0)
    {
      lablesWithVolume++;

      mitk::Color color (GetLabelColor(lablesWithVolume));
      lut->SetColorForLabel(i,color.GetRed(), color.GetGreen(), color.GetBlue(), 1.0);

      QColor qcolor;
      qcolor.setRgbF(color.GetRed(), color.GetGreen(), color.GetBlue(), 0.7);

      //output volume as string "x.xx ml"
      std::stringstream stream;
      stream<< std::fixed << std::setprecision(2)<<volumes.at(i)/1000;
      stream<<" ml";

      QListWidgetItem* item = new QListWidgetItem();
      item->setText(QString::fromStdString(stream.str()));
      item->setBackgroundColor(qcolor);
      m_Controls.volumeList->addItem(item);
    }
  }

  //set the rendering mode to use the lookup table and level window
  clippedNode->SetProperty("Image Rendering.Mode", mitk::RenderingModeProperty::New(mitk::RenderingModeProperty::LOOKUPTABLE_LEVELWINDOW_COLOR));
  mitk::LookupTableProperty::Pointer lutProp = mitk::LookupTableProperty::New(lut.GetPointer());
  clippedNode->SetProperty("LookupTable", lutProp);
  // it is absolutely important, to use the LevelWindow settings provided by
  // the LUT generator, otherwise, it is not guaranteed, that colors show
  // up correctly.
  clippedNode->SetProperty("levelwindow", mitk::LevelWindowProperty::New(lut->GetLevelWindow()));
}

void QmitkDeformableClippingPlaneView::OnSelectClippedSegment(QListWidgetItem* item)
{
  int itemNumber = item->listWidget()->row(item); //Get row of clicked item, 0 indexed
  
  //Get underlying clipped image 
  mitk::DataNode::Pointer clippedNode = this->GetDataStorage()->GetNamedNode("Clipped Image"); 
  mitk::Image::Pointer writeImage = dynamic_cast<mitk::Image *>(clippedNode->GetData()); 
  
  //Get reference node and generate tool for creating segmentation
  mitk::DataNode::Pointer node = m_ReferenceNode; 
  mitk::Image::Pointer image = dynamic_cast<mitk::Image*>( node->GetData() );  
  mitk::ToolManager* toolManager = mitk::ToolManagerProvider::GetInstance()->GetToolManager(); 
  mitk::Tool* firstTool = toolManager->GetToolById(0);
  
  //Name and color of new segmentation (default red)
  mitk::Color color;
  color.SetRed(1);
  color.SetBlue(0);
  color.SetGreen(0);
  std::string nodename = "clippedSegment_"+std::to_string(itemNumber+1);
  
  //Create new node
  mitk::DataNode::Pointer emptySegmentation = firstTool->CreateEmptySegmentationNode(image, nodename, color); 
  emptySegmentation->SetProperty( "showVolume", mitk::BoolProperty::New( false ) ); 
  if(mitk::ToolManagerProvider::GetInstance()->GetToolManager()->GetWorkingData(0))
  {
    mitk::ToolManagerProvider::GetInstance()->GetToolManager()->GetWorkingData(0)->SetSelected(false);
  }
  emptySegmentation->SetSelected(true); 

  this->GetDataStorage()->Add( emptySegmentation, node ); 

  this->FireNodeSelected( emptySegmentation );
  mitk::RenderingManager::GetInstance()->InitializeViews(emptySegmentation->GetData()->GetTimeGeometry(), mitk::RenderingManager::REQUEST_UPDATE_ALL, true);
  
  mitk::Image::Pointer saveImage = dynamic_cast<mitk::Image *>(emptySegmentation->GetData());
  
  int maxLabelValue = writeImage->GetStatistics()->GetScalarValueMax();
  
  AccessByItk_3(writeImage, ITKThresholding, saveImage, itemNumber, maxLabelValue); //Use Thresholding to isolate the label number

}

template <typename TPixel, unsigned int VImageDimension>
void QmitkDeformableClippingPlaneView::ITKThresholding(itk::Image<TPixel, VImageDimension> *originalImage,
                            mitk::Image *segmentation,
                            int labelNumber,
                            int maxLabelValue)
{
  typedef itk::Image<TPixel, VImageDimension> ImageType;
  typedef itk::BinaryThresholdImageFilter<ImageType, ImageType> ThresholdFilterType;
  typedef itk::Statistics::ImageToHistogramFilter< ImageType > ImageToHistogramFilterType;
  
  typename ImageToHistogramFilterType::Pointer imageToHistogramFilter = ImageToHistogramFilterType::New();
  
  typename ImageToHistogramFilterType::HistogramType::SizeType size(1);
  size.Fill(maxLabelValue+1);
  
  imageToHistogramFilter->SetInput( originalImage );
  imageToHistogramFilter->SetHistogramSize( size );
  imageToHistogramFilter->Update();
  
  typename ImageToHistogramFilterType::HistogramType* histogram = imageToHistogramFilter->GetOutput();
  
  int threshold = 0;
  int i = 0;
  while(i < labelNumber+1)
    {
    threshold++;    
    if( histogram->GetFrequency(threshold) != 0)
      {
      i++;
      }
    }

  typename ThresholdFilterType::Pointer filter = ThresholdFilterType::New();
  filter->SetInput(originalImage);
  filter->SetLowerThreshold(threshold);
  filter->SetUpperThreshold(threshold);
  filter->SetInsideValue(1);
  filter->SetOutsideValue(0);
  filter->Update();

  segmentation->SetVolume((void *)(filter->GetOutput()->GetPixelContainer()->GetBufferPointer()));
}

mitk::DataStorage::SetOfObjects::ConstPointer QmitkDeformableClippingPlaneView::GetAllClippingPlanes()
{
  mitk::NodePredicateProperty::Pointer clipPredicate= mitk::NodePredicateProperty::New("clippingPlane",mitk::BoolProperty::New(true));
  mitk::DataStorage::SetOfObjects::ConstPointer allPlanes = GetDataStorage()->GetSubset(clipPredicate);
  return allPlanes;
}

mitk::Color QmitkDeformableClippingPlaneView::GetLabelColor(int label)
{
  float red, green, blue;
  switch ( label % 6 )
  {
  case 0:
    {red = 1.0; green = 0.0; blue = 0.0; break;}
  case 1:
    {red = 0.0; green = 1.0; blue = 0.0; break;}
  case 2:
    {red = 0.0; green = 0.0; blue = 1.0;break;}
  case 3:
    {red = 1.0; green = 1.0; blue = 0.0;break;}
  case 4:
    {red = 1.0; green = 0.0; blue = 1.0;break;}
  case 5:
    {red = 0.0; green = 1.0; blue = 1.0;break;}
  default:
    {red = 0.0; green = 0.0; blue = 0.0;}
  }

  float tmp[3] = { red, green, blue };

  double factor;

  int outerCycleNr = label / 6;
  int cycleSize = pow(2.0,(int)(log((double)(outerCycleNr))/log( 2.0 )));
  if (cycleSize==0)
    cycleSize = 1;
  int insideCycleCounter = outerCycleNr % cycleSize;

  if ( outerCycleNr == 0)
    factor = 255;
  else
    factor = ( 256 / ( 2 * cycleSize ) ) + ( insideCycleCounter * ( 256 / cycleSize ) );

  tmp[0]= tmp[0]/256*factor;
  tmp[1]= tmp[1]/256*factor;
  tmp[2]= tmp[2]/256*factor;

  return mitk::Color(tmp);
}

void QmitkDeformableClippingPlaneView::OnTranslationMode(bool check)
{
  if(check)
  { //uncheck all other buttons
    m_Controls.rotationPushButton->setChecked(false);
    m_Controls.deformationPushButton->setChecked(false);

    mitk::ClippingPlaneInteractor3D::Pointer affineDataInteractor = mitk::ClippingPlaneInteractor3D::New();
    affineDataInteractor->LoadStateMachine("ClippingPlaneInteraction3D.xml", us::ModuleRegistry::GetModule("MitkDataTypesExt"));
    affineDataInteractor->SetEventConfig("ClippingPlaneTranslationConfig.xml", us::ModuleRegistry::GetModule("MitkDataTypesExt"));
    affineDataInteractor->SetDataNode(m_WorkingNode);
  }
  else
    m_WorkingNode->SetDataInteractor(nullptr);
}

void QmitkDeformableClippingPlaneView::OnRotationMode(bool check)
{
  if(check)
  { //uncheck all other buttons
    m_Controls.translationPushButton->setChecked(false);
    m_Controls.deformationPushButton->setChecked(false);

    mitk::ClippingPlaneInteractor3D::Pointer affineDataInteractor = mitk::ClippingPlaneInteractor3D::New();
    affineDataInteractor->LoadStateMachine("ClippingPlaneInteraction3D.xml", us::ModuleRegistry::GetModule("MitkDataTypesExt"));
    affineDataInteractor->SetEventConfig("ClippingPlaneRotationConfig.xml", us::ModuleRegistry::GetModule("MitkDataTypesExt"));
    affineDataInteractor->SetDataNode(m_WorkingNode);
  }
  else
    m_WorkingNode->SetDataInteractor(nullptr);
}

void QmitkDeformableClippingPlaneView::OnDeformationMode(bool check)
{
  if(check)
  { //uncheck all other buttons
    m_Controls.translationPushButton->setChecked(false);
    m_Controls.rotationPushButton->setChecked(false);

    mitk::SurfaceDeformationDataInteractor3D::Pointer surfaceDataInteractor = mitk::SurfaceDeformationDataInteractor3D::New();
    surfaceDataInteractor->LoadStateMachine("ClippingPlaneInteraction3D.xml", us::ModuleRegistry::GetModule("MitkDataTypesExt"));
    surfaceDataInteractor->SetEventConfig("ClippingPlaneDeformationConfig.xml", us::ModuleRegistry::GetModule("MitkDataTypesExt"));
    surfaceDataInteractor->SetDataNode(m_WorkingNode);
  }
  else
    m_WorkingNode->SetDataInteractor(nullptr);
}

void QmitkDeformableClippingPlaneView::DeactivateInteractionButtons()
{
  m_Controls.translationPushButton->setChecked(false);
  m_Controls.rotationPushButton->setChecked(false);
  m_Controls.deformationPushButton->setChecked(false);
}
