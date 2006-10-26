/*=========================================================================

Program:   Medical Imaging & Interaction Toolkit
Module:    $RCSfile$
Language:  C++
Date:      $Date$
Version:   $Revision$

Copyright (c) German Cancer Research Center, Division of Medical and
Biological Informatics. All rights reserved.
See MITKCopyright.txt or http://www.mitk.org/copyright.html for details.

This software is distributed WITHOUT ANY WARRANTY; without even
the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
PURPOSE.  See the above copyright notices for more information.

=========================================================================*/


#include "mitkItkImageFileReader.h"
#include "mitkConfig.h"

#include <itkImageFileReader.h>
#include <itksys/SystemTools.hxx>
#include <itksys/Directory.hxx>
#include <itkImage.h>
//#include <itkImageSeriesReader.h>
#include <itkImageFileReader.h>
#include <itkImageIOFactory.h>
#include <itkImageIORegion.h>
//#include <itkImageSeriesReader.h>
//#include <itkDICOMImageIO2.h>
//#include <itkDICOMSeriesFileNames.h>
//#include <itkGDCMImageIO.h>
//#include <itkGDCMSeriesFileNames.h>
//#include <itkNumericSeriesFileNames.h>


void mitk::ItkImageFileReader::GenerateData()
{
  mitk::Image::Pointer image = this->GetOutput();

  const unsigned int MINDIM = 2;
  const unsigned int MAXDIM = 4;

  std::cout << "loading " << m_FileName << " via itk::ImageIOFactory... " << std::endl;

  // Check to see if we can read the file given the name or prefix
  if ( m_FileName == "" )
  {
    itkWarningMacro( << "File Type not supported!" );
    return ;
  }

  if ( m_FileName == "--no-server" )
  {
    return ;
  }

  itk::ImageIOBase::Pointer imageIO = itk::ImageIOFactory::CreateImageIO( m_FileName.c_str(), itk::ImageIOFactory::ReadMode );
  if ( imageIO.IsNull() )
  {
    itkWarningMacro( << "File Type not supported!" );
    return ;
  }

  // Got to allocate space for the image. Determine the characteristics of
  // the image.
  imageIO->SetFileName( m_FileName.c_str() );
  imageIO->ReadImageInformation();

  unsigned int ndim = imageIO->GetNumberOfDimensions();
  if ( ndim < MINDIM || ndim > MAXDIM )
  {
    itkWarningMacro( << "Sorry, only dimensions 2, 3 and 4 are supported. The given file has " << ndim << " dimensions! Reading as 4D." );
    ndim = MAXDIM;
  }

  itk::ImageIORegion ioRegion( ndim );
  itk::ImageIORegion::SizeType ioSize = ioRegion.GetSize();
  itk::ImageIORegion::IndexType ioStart = ioRegion.GetIndex();

  unsigned int dimensions[ MAXDIM ];
  dimensions[ 0 ] = 0;
  dimensions[ 1 ] = 0;
  dimensions[ 2 ] = 0;
  dimensions[ 3 ] = 0;

  float spacing[ MAXDIM ];
  spacing[ 0 ] = 1.0f;
  spacing[ 1 ] = 1.0f;
  spacing[ 2 ] = 1.0f;
  spacing[ 3 ] = 1.0f;

  Point3D origin;
  origin.Fill(0);

  for ( unsigned int i = 0; i < ndim ; ++i )
  {
    ioStart[ i ] = 0;
    ioSize[ i ] = imageIO->GetDimensions( i );
    if(i<MAXDIM)
    {
      dimensions[ i ] = imageIO->GetDimensions( i );
      spacing[ i ] = imageIO->GetSpacing( i );
      if(spacing[ i ] <= 0)
        spacing[ i ] = 1.0f;
    }
    if(i<3)
    {
      origin[ i ] = imageIO->GetOrigin( i );
    }
  }

  ioRegion.SetSize( ioSize );
  ioRegion.SetIndex( ioStart );

  std::cout << "ioRegion: " << ioRegion << std::endl;
  imageIO->SetIORegion( ioRegion );
  void* buffer = new unsigned char[imageIO->GetImageSizeInBytes()];
  imageIO->Read( buffer );
  //mitk::Image::Pointer image = mitk::Image::New();
  if((ndim==4) && (dimensions[3]<=1))
    ndim = 3;
  if((ndim==3) && (dimensions[2]<=1))
    ndim = 2;
#if ITK_VERSION_MAJOR == 2 || ( ITK_VERSION_MAJOR == 1 && ITK_VERSION_MINOR > 6 )
  mitk::PixelType pixelType( imageIO->GetComponentTypeInfo(), imageIO->GetNumberOfComponents() );
  image->Initialize( pixelType, ndim, dimensions );
#else
  if ( imageIO->GetNumberOfComponents() > 1)
    itkWarningMacro(<<"For older itk versions, only scalar images are supported!");
  mitk::PixelType pixelType( imageIO->GetPixelType() );
  image->Initialize( pixelType, ndim, dimensions );
#endif 
  image->SetImportVolume( buffer, 0, 0, Image::ManageMemory );

#if ITK_VERSION_MAJOR == 2 && ITK_VERSION_MINOR < 4
  image->GetSlicedGeometry()->SetOrigin( origin );
  image->GetSlicedGeometry()->SetSpacing( spacing );
  image->GetTimeSlicedGeometry()->InitializeEvenlyTimed(image->GetSlicedGeometry(), image->GetDimension(3));
#else
  // access direction of itk::Image and include spacing
  mitk::Matrix3D matrix;
  matrix.SetIdentity();
  unsigned int i, j, itkDimMax3 = (ndim >= 3? 3 : ndim);
  for ( i=0; i < itkDimMax3; ++i)
    for( j=0; j < itkDimMax3; ++j )
      matrix[i][j] = imageIO->GetDirection(j)[i];

  // re-initialize PlaneGeometry with origin and direction
  PlaneGeometry* planeGeometry = static_cast<PlaneGeometry*>(image->GetSlicedGeometry(0)->GetGeometry2D(0));
  planeGeometry->SetOrigin(origin);
  planeGeometry->GetIndexToWorldTransform()->SetMatrix(matrix);

  // re-initialize SlicedGeometry3D
  SlicedGeometry3D* slicedGeometry = image->GetSlicedGeometry(0);
  slicedGeometry->InitializeEvenlySpaced(planeGeometry, image->GetDimension(2));
  slicedGeometry->SetSpacing(spacing);

  // re-initialize TimeSlicedGeometry
  image->GetTimeSlicedGeometry()->InitializeEvenlyTimed(slicedGeometry, image->GetDimension(3));
#endif
  buffer = NULL;
  std::cout << "number of image components: "<< image->GetPixelType().GetNumberOfComponents() << std::endl;
//  mitk::DataTreeNode::Pointer node = this->GetOutput();
//  node->SetData( image );

  // add level-window property
  //if ( image->GetPixelType().GetNumberOfComponents() == 1 )
  //{
  //  SetDefaultImageProperties( node );
  //} 
  std::cout << "...finished!" << std::endl;
}


bool mitk::ItkImageFileReader::CanReadFile(const std::string filename, const std::string filePrefix, const std::string filePattern) 
{
  // First check the extension
  if(  filename == "" )
    return false;

  // check if image is serie
  if( filePattern != "" && filePrefix != "" )
    return false;

  itk::ImageIOBase::Pointer imageIO = itk::ImageIOFactory::CreateImageIO( filename.c_str(), itk::ImageIOFactory::ReadMode );
  if ( imageIO.IsNull() )
    return false;

  return true;
}

mitk::ItkImageFileReader::ItkImageFileReader()
    : m_FileName(""), m_FilePrefix(""), m_FilePattern("")
{
}

mitk::ItkImageFileReader::~ItkImageFileReader()
{
}
