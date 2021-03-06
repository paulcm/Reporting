/*==============================================================================

  Program: 3D Slicer

  Portions (c) Copyright Brigham and Women's Hospital (BWH) All Rights Reserved.

  See COPYRIGHT.txt
  or http://www.slicer.org/copyright/copyright.txt for details.

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

==============================================================================*/

// ModuleTemplate includes
#include "vtkSlicerReportingModuleLogic.h"

// MRML includes
#include <vtkMRMLAnnotationNode.h>
#include <vtkMRMLAnnotationControlPointsNode.h>
#include <vtkMRMLAnnotationFiducialNode.h>
#include <vtkMRMLAnnotationRulerNode.h>
#include <vtkMRMLColorNode.h>
#include <vtkMRMLVolumeArchetypeStorageNode.h>
#include <vtkMRMLDisplayNode.h>
#include <vtkMRMLReportingReportNode.h>
#include <vtkMRMLScalarVolumeNode.h>
#include <vtkMRMLScriptedModuleNode.h>

// VTK includes
#include <vtkImageData.h>
#include <vtkMatrix4x4.h>
#include <vtkNew.h>
#include <vtkSmartPointer.h>
#include <vtksys/SystemTools.hxx>
#include <vtkPointData.h>
#include <vtkImageCast.h>

// Qt includes
#include <QDomDocument>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QtXml>

// CTK includes
#include <ctkDICOMDatabase.h>
#include <ctkDICOMIndexer.h>

// STD includes
#include <cassert>
#include <time.h>

// DCMTK includes
#include <dcmtk/dcmdata/dcmetinf.h>
#include <dcmtk/dcmdata/dcfilefo.h>
#include <dcmtk/dcmdata/dcuid.h>
#include <dcmtk/dcmdata/dcdict.h>
#include <dcmtk/dcmdata/cmdlnarg.h>
#include <dcmtk/ofstd/ofconapp.h>
#include <dcmtk/ofstd/ofstd.h>
#include <dcmtk/ofstd/ofdatime.h>
#include <dcmtk/dcmdata/dcuid.h>         /* for dcmtk version name */
#include <dcmtk/dcmdata/dcdeftag.h>      /* for DCM_StudyInstanceUID */
#include <dcmtk/dcmdata/dcvrda.h>        /* for DcmDate */
#include <dcmtk/dcmdata/dcvrtm.h>        /* for DcmTime */
#include <dcmtk/dcmdata/dcvrat.h>        /* for DcmAttribute */

// SlicerApp includes
#include "qSlicerApplication.h"

#include "vtkReportingVersionConfigure.h"


//----------------------------------------------------------------------------
vtkStandardNewMacro(vtkSlicerReportingModuleLogic);

//----------------------------------------------------------------------------
vtkSlicerReportingModuleLogic::vtkSlicerReportingModuleLogic()
{
  this->ActiveParameterNodeID = NULL;
  this->ErrorMessage = NULL;
  this->DICOMDatabase = NULL;
  this->GUIHidden = 1;

  vtkDebugMacro("********* vtkSlicerReportingModuleLogic Constructor **********");
}

//----------------------------------------------------------------------------
vtkSlicerReportingModuleLogic::~vtkSlicerReportingModuleLogic()
{
  if (this->ActiveParameterNodeID)
    {
    delete [] this->ActiveParameterNodeID;
    this->ActiveParameterNodeID = NULL;
    }
  if (this->ErrorMessage)
    {
    delete [] this->ErrorMessage;
    this->ErrorMessage = NULL;
    }
}

//----------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);

  os << indent << "Active Parameter Node ID = " << (this->ActiveParameterNodeID ? this->ActiveParameterNodeID : "null") << std::endl;
  os << indent << "Error Message " << (this->ErrorMessage ? this->GetErrorMessage() : "none") << "\n";
  os << indent << "GUI Hidden = " << (this->GUIHidden ? "true" : "false") << "\n";

}

//---------------------------------------------------------------------------
bool vtkSlicerReportingModuleLogic::InitializeDICOMDatabase(std::string dbPath)
{
  if(this->DICOMDatabase)
    {
    this->DICOMDatabase->closeDatabase();
    }
  else
    {
    this->DICOMDatabase = new ctkDICOMDatabase();
    }

  this->DICOMDatabase->openDatabase(dbPath.c_str(), "Reporting.Logic");
  return this->DICOMDatabase->isOpen();
}

//---------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::SetMRMLSceneInternal(vtkMRMLScene* newScene)
{
  vtkDebugMacro("SetMRMLSceneInternal");

  vtkNew<vtkIntArray> events;
  events->InsertNextValue(vtkMRMLScene::NodeAddedEvent);
  events->InsertNextValue(vtkMRMLScene::NodeRemovedEvent);
  events->InsertNextValue(vtkMRMLScene::EndBatchProcessEvent);
  this->SetAndObserveMRMLSceneEventsInternal(newScene, events.GetPointer());
}

//-----------------------------------------------------------------------------
// Register all module-specific nodes here
void vtkSlicerReportingModuleLogic::RegisterNodes()
{
  if(!this->GetMRMLScene())
    return;
  vtkMRMLReportingReportNode *rn = vtkMRMLReportingReportNode::New();
  this->GetMRMLScene()->RegisterNodeClass(rn);
  rn->Delete();
}

//---------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::ProcessMRMLNodesEvents(vtkObject *vtkNotUsed(caller),
                                                            unsigned long event,
                                                            void *callData)
{
  vtkDebugMacro("ProcessMRMLNodesEvents");

  vtkMRMLNode* node = reinterpret_cast<vtkMRMLNode*> (callData);
  vtkMRMLAnnotationNode* annotationNode = vtkMRMLAnnotationNode::SafeDownCast(node);
  if (annotationNode)
    {
    switch (event)
      {
      case vtkMRMLScene::NodeAddedEvent:
        this->OnMRMLSceneNodeAdded(annotationNode);
        break;
      case vtkMRMLScene::NodeRemovedEvent:
        this->OnMRMLSceneNodeRemoved(annotationNode);
        break;
      }
    }
}

//---------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::UpdateFromMRMLScene()
{
  assert(this->GetMRMLScene() != 0);
}

//---------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::AddNodeToReport(vtkMRMLNode* node)
{
  this->OnMRMLSceneNodeAdded(node);
}

//---------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::OnMRMLSceneNodeAdded(vtkMRMLNode* node)
{ 
  if (!node)
    {
    return;
    }

  std::string annotationType;

  // get the active report
  std::string activeReportID = this->GetActiveReportID();
  if(!activeReportID.compare(""))
    {
    return;
    }
  
  vtkMRMLReportingReportNode *reportNode = NULL;
  vtkMRMLNode *mrmlNode = this->GetMRMLScene()->GetNodeByID(activeReportID.c_str());
  if (mrmlNode)
    {
    reportNode = vtkMRMLReportingReportNode::SafeDownCast(mrmlNode);
    }
  // exit if there is no report
  if(!reportNode)
    {
    vtkDebugMacro("Report node does not exist. Exiting.");
    return;
    }
  //  or if there is no volume associated with the report
  if(!this->GetVolumeIDForReportNode(reportNode))
    {
    vtkDebugMacro("No volume is assigned to the report " << reportNode->GetID() << ". Exiting.");
    return;
    }

  // cast to the supported node types
  vtkMRMLAnnotationFiducialNode *fiducialNode = vtkMRMLAnnotationFiducialNode::SafeDownCast(node);
  vtkMRMLAnnotationRulerNode *rulerNode = vtkMRMLAnnotationRulerNode::SafeDownCast(node);
  vtkMRMLScalarVolumeNode *labelVolumeNode = vtkMRMLScalarVolumeNode::SafeDownCast(node);

  if(!fiducialNode && !rulerNode && !(labelVolumeNode && labelVolumeNode->GetLabelMap()))
    {
    // the node added should be ignored
    return;
    }
  

  // handle new fiducials and rulers
  if(fiducialNode || rulerNode)
    {
    // only want to grab annotation nodes if the gui is visible
    if (this->GetGUIHidden())
      {
      return;
      }
    vtkDebugMacro("OnMRMLSceneNodeAdded: gui is not hidden, got an annotation node added with id " << node->GetID());

    /// check that the annotation was placed on the current acquisition plane
    /// according to the parameter node
    vtkMRMLScriptedModuleNode *parameterNode = NULL;
    vtkMRMLNode *mrmlNode = this->GetMRMLScene()->GetNodeByID(this->GetActiveParameterNodeID());
    std::string acquisitionSliceViewer;
    if (mrmlNode)
      {
      parameterNode = vtkMRMLScriptedModuleNode::SafeDownCast(mrmlNode);
      if (parameterNode)
        {
        acquisitionSliceViewer = parameterNode->GetParameter("acquisitionSliceViewer");
        if (acquisitionSliceViewer.compare("") != 0)
          {
          std::cout << "Parameter node has acquisition plane = '" << acquisitionSliceViewer.c_str() << "'" << std::endl;
          }
        }
      }

    /// check that the annotation has a valid UID
    vtkMRMLAnnotationNode *annotationNode = vtkMRMLAnnotationNode::SafeDownCast(node);
    std::string UID = this->GetSliceUIDFromMarkUp(annotationNode);
    if(UID.compare("NONE") == 0)
      { 
      std::string errorMessage;
      errorMessage = std::string("Newly added markup '");
      errorMessage += std::string( node->GetName());
      errorMessage += std::string("' isn't associated with a single UID from a volume. ");
      std::string userMessage = std::string("Newly added markup was not placed on a single scan plane. ");
      if (reportNode && reportNode->GetAllowOutOfPlaneMarkups())
        {
        errorMessage += std::string("Not using it for this report.");
        userMessage += std::string("Not using it for this report.");
        }
      else
        {
        errorMessage += std::string("It has been hidden.");
        userMessage += std::string("It has been hidden.");
        // removing the node can cause a crash due to the string of events,
        // just hide it
        // this->GetMRMLScene()->RemoveNode(node);
        if (annotationNode)
          {
          annotationNode->SetDisplayVisibility(0);
          }
        }
      vtkDebugMacro(<< errorMessage.c_str());
      // let the GUI know by invoking an event
      this->SetErrorMessage(userMessage.c_str());
      vtkDebugMacro("Logic: Invoking ErrorEvent");
      this->InvokeEvent(vtkSlicerReportingModuleLogic::ErrorEvent, (void *)(userMessage.c_str()));
      return;
      }
    else
      {
      this->SetErrorMessage("");
      }
    
    if (fiducialNode)
      {
      annotationType = "Fiducial";
      }
    if(rulerNode)
      {
      annotationType = "Ruler";
      }
    // set it to be part of this report
    node->SetAttribute("ReportingReportNodeID", activeReportID.c_str());
    vtkDebugMacro("Annotation node added, associated node id = " << node->GetAttribute("AssociatedNodeID"));
    }

  // handle new label node
  else if (labelVolumeNode && labelVolumeNode->GetLabelMap())
    {
    annotationType = "Segmentation";

    const char *associatedNodeID = node->GetAttribute("AssociatedNodeID");
    if (associatedNodeID)
      {
      vtkDebugMacro("OnMRMLSceneNodeAdded: have a label map volume with associated id of " << associatedNodeID);
      // is that volume under the active report?
      const char *volumeID = NULL;
      volumeID = this->GetVolumeIDForReportNode(reportNode);

      if (strcmp(volumeID, associatedNodeID) == 0)
        {
        // the new label map is associated with the volume in this report,
        // so set the report node attribute
        labelVolumeNode->SetAttribute("ReportingReportNodeID", activeReportID.c_str());
        }
      else
        {
        vtkDebugMacro("OnMRMLSceneNodeAdded: associated volume " << associatedNodeID << " is not the volume for this report: " << volumeID);
        }
      }
    else
      {
      vtkDebugMacro("OnMRMLSceneNodeAdded: no associated node id on scalar volume");
      }
    }

  // rename it from the reporting node
  vtkMRMLColorNode *colorNode = vtkMRMLColorNode::SafeDownCast(this->GetMRMLScene()->GetNodeByID(reportNode->GetColorNodeID()));
  if (!colorNode)
    {
    std::cerr << "Failed to get label decription" << std::endl;
    return;
    }

  const char *desc = colorNode->GetColorName(reportNode->GetFindingLabel());
  std::string annotationName;
  if (desc)
    {
    annotationName = std::string(desc)+"_"+annotationType;
    }
  else
    {
    annotationName = std::string("Report_") + annotationType;
    }
  node->SetName(node->GetScene()->GetUniqueNameByString(annotationName.c_str()));

  // TODO: sanity check to make sure that the annotation's AssociatedNodeID
  // attribute points to the current volume
  
  // let the GUI know there's a new annotation node
  std::cout << "Reporting Logic: had an annotation added, invoking node added event for " << node->GetName() << std::endl;
  this->InvokeEvent(vtkSlicerReportingModuleLogic::AnnotationAdded); // vtkMRMLScene::NodeAddedEvent);
  
}

//---------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::OnMRMLSceneNodeRemoved(vtkMRMLNode* node)
{
  if (!node)
    {
    return;
    }
  if (this->GetMRMLScene()->IsBatchProcessing())
    {
    return;
    }
  if (node->IsA("vtkMRMLReportingReportNode"))
    {
    vtkDebugMacro("OnMRMLSceneNodeRemoved: node id = " << node->GetID());
    // find the markups associated with it?
    }
}

//---------------------------------------------------------------------------
std::string vtkSlicerReportingModuleLogic::GetSliceUIDFromMarkUp(vtkMRMLAnnotationNode *node)
{
  std::string UID = std::string("NONE");

  if (!node)
    {
    vtkErrorMacro("GetSliceUIDFromMarkUp: no input node!");
    return  UID;
    }

  if (!this->GetMRMLScene())
    {
    vtkErrorMacro("GetSliceUIDFromMarkUp: No MRML Scene defined!");
    return UID;
    }
  
  vtkMRMLAnnotationControlPointsNode *cpNode = vtkMRMLAnnotationControlPointsNode::SafeDownCast(node);
  if (!node)
    {
    vtkErrorMacro("GetSliceUIDFromMarkUp: Input node is not a control points node!");
    return UID;
    }
  
  int numPoints = cpNode->GetNumberOfControlPoints();
  vtkDebugMacro("GetSliceUIDFromMarkUp: have a control points node with " << numPoints << " points");

  // get the associated node
  const char *associatedNodeID = cpNode->GetAttribute("AssociatedNodeID");
  if (!associatedNodeID)
    {
    vtkDebugMacro("GetSliceUIDFromMarkUp: No AssociatedNodeID on the annotation node");
    return UID;
    }
  vtkMRMLScalarVolumeNode *volumeNode = NULL;
  vtkMRMLNode *mrmlNode = this->GetMRMLScene()->GetNodeByID(associatedNodeID);
  if (!mrmlNode)
    {
    vtkErrorMacro("GetSliceUIDFromMarkUp: Associated node not found by id: " << associatedNodeID);
    return UID;
    }
  volumeNode = vtkMRMLScalarVolumeNode::SafeDownCast(mrmlNode);
  if (!volumeNode)
    {
    vtkErrorMacro("GetSliceUIDFromMarkUp: Associated node with id: " << associatedNodeID << " is not a volume node!");
    return UID;
    }

  // get the list of UIDs from the volume
  if (!volumeNode->GetAttribute("DICOM.instanceUIDs"))
    {
    vtkErrorMacro("GetSliceUIDFromMarkUp: Volume node with id: " << associatedNodeID << " doesn't have a list of UIDs under the attribute DICOM.instanceUIDs! Returning '" << UID.c_str() << "'");
    return UID;
    }
  std::string uidsString = volumeNode->GetAttribute("DICOM.instanceUIDs");
  // break them up into a vector, they're space separated
  std::vector<std::string> uidVector;
  char *uids = new char[uidsString.size()+1];
  strcpy(uids,uidsString.c_str());
  char *ptr;
  ptr = strtok(uids, " ");
  while (ptr != NULL)
    {
    vtkDebugMacro("Parsing UID = " << ptr);
    uidVector.push_back(std::string(ptr));
    ptr = strtok(NULL, " ");
    }
  
  // get the RAS to IJK matrix from the volume
  vtkSmartPointer<vtkMatrix4x4> ras2ijk = vtkSmartPointer<vtkMatrix4x4>::New();
  volumeNode->GetRASToIJKMatrix(ras2ijk);

  // need to make sure that all the UIDs are the same
  std::string UIDi = std::string("NONE");
  for (int i = 0; i < numPoints; i++)
    {
    // get the RAS point
    double ras[4] = {0.0, 0.0, 0.0, 1.0};
    cpNode->GetControlPointWorldCoordinates(i, ras);
    // convert point from ras to ijk
    double ijk[4] = {0.0, 0.0, 0.0, 1.0};
    ras2ijk->MultiplyPoint(ras, ijk);
    vtkDebugMacro("Point " << i << " ras = " << ras[0] << ", " << ras[1] << ", " << ras[2] << " converted to ijk  = " << ijk[0] << ", " << ijk[1] << ", " << ijk[2] << ", getting uid at index " << ijk[2] << " (uid vector size = " << uidVector.size() << ")");
    unsigned int k = static_cast<unsigned int>(floor(ijk[2]+0.5));
    vtkDebugMacro("\tusing ijk[2] " << ijk[2] << " as an unsigned int: " << k);
    if (uidVector.size() > k)
      {
      // assumption is that the dicom UIDs are in order by k
      UIDi = uidVector[k];
      }
    else
      {
      // multiframe data? set UID to the first one, but will need to store the
      // frame number on AIM import
      UIDi = uidVector[0];
      }
    if (i == 0)
      {
      UID = UIDi;
      }
    else
      {
      // check if UIDi does not match UID
      // AF: wouldn't it be better to return the list of UIDs?
      if (UIDi.compare(UID) != 0)
        {
        vtkWarningMacro("GetSliceUIDFromMarkUp: annotation " << cpNode->GetName() << " point " << i << " has a UID of:\n" << UIDi.c_str() << "\nthat doesn't match previous UIDs of:\n" << UID.c_str() << "\n\tReturning UID of NONE");
        UID = std::string("NONE");
        break;
        }
      }
    }  
  return UID;

}
/*
//---------------------------------------------------------------------------
char *vtkSlicerReportingModuleLogic::GetTopLevelHierarchyNodeID()
{
  if (this->GetMRMLScene() == NULL)
    {
    return NULL;
    }
  const char *topLevelName = "Reporting Hierarchy";

  /// check for a top level hierarchy
  if (!this->GetMRMLScene()->GetFirstNodeByName(topLevelName))
    {
    vtkMRMLDisplayableHierarchyNode *reportingHierarchy = vtkMRMLDisplayableHierarchyNode::New();
    reportingHierarchy->HideFromEditorsOff();
    reportingHierarchy->SetName(topLevelName);
    reportingHierarchy->SetScene(this->GetMRMLScene());
    this->GetMRMLScene()->AddNode(reportingHierarchy);
    reportingHierarchy->Delete();
    }
   
  char *toplevelNodeID =  this->GetMRMLScene()->GetFirstNodeByName(topLevelName)->GetID();;

  return toplevelNodeID;
}

//---------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::InitializeHierarchyForReport(vtkMRMLReportingReportNode *node)
{
  if (!node)
    {
    vtkErrorMacro("InitializeHierarchyForReport: null input report");
    return;
    }

  if (!node->GetScene() || !node->GetID())
    {
    vtkErrorMacro("InitializeHierarchyForReport: No MRML Scene defined on node, or else it doesn't have an id");
    return;
    }

  vtkDebugMacro("InitializeHierarchyForReport: setting up hierarchy for report " << node->GetID());

  /// does the node already have a hierarchy set up for it?
  vtkMRMLHierarchyNode *hnode = vtkMRMLHierarchyNode::GetAssociatedHierarchyNode(node->GetScene(), node->GetID());
  if (hnode)
    {
    vtkDebugMacro("InitializeHierarchyForReport: report " << node->GetID() << " already has a hierarchy associated with it, " << hnode->GetID());
    return;
    }
    /// otherwise, create a 1:1 hierarchy for this node
  vtkMRMLDisplayableHierarchyNode *reportHierarchyNode = vtkMRMLDisplayableHierarchyNode::New();
  /// it's a stealth node:
  reportHierarchyNode->HideFromEditorsOn();
  std::string hnodeName = std::string(node->GetName()) + std::string(" Hierarchy");
  reportHierarchyNode->SetName(this->GetMRMLScene()->GetUniqueNameByString(hnodeName.c_str()));
  reportHierarchyNode->SetScene(this->GetMRMLScene());
  this->GetMRMLScene()->AddNode(reportHierarchyNode);

  
  // make it the child of the top level reporting node
  const char *topLevelID = this->GetTopLevelHierarchyNodeID();
  vtkDebugMacro("InitializeHierarchyForReport: pointing report hierarchy node at top level id " << (topLevelID ? topLevelID : "null"));
  reportHierarchyNode->SetParentNodeID(topLevelID);
  
  // set the displayable node id to point to this report node
  node->SetDisableModifiedEvent(1);
  reportHierarchyNode->SetDisplayableNodeID(node->GetID());
  node->SetDisableModifiedEvent(0);

 
  reportHierarchyNode->Delete();
}
*/

//---------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::AddVolumeToReport(vtkMRMLVolumeNode *node)
{
  if (!node)
    {
    vtkErrorMacro("AddVolumeToReport: null input volume");
    return;
    }

  if (!node->GetScene() || !node->GetID())
    {
    vtkErrorMacro("AddVolumeToReport: No MRML Scene defined on node, or else it doesn't have an id");
    return;
    }

  std::string reportID = this->GetActiveReportID();
  if (reportID.compare("") == 0)
    {
    vtkWarningMacro("No active report to which to add volume " << node->GetName());
    return;
    }
  vtkDebugMacro("AddVolumeToReport: associating volume " << node->GetID() << " with report " << reportID.c_str());
  if (node->GetAttribute("ReportingReportNodeID") != NULL)
    {
    vtkWarningMacro("Volume " << node->GetName() << " was already in a report, moving it to this one");
    }
  node->SetAttribute("ReportingReportNodeID", reportID.c_str());
}

//---------------------------------------------------------------------------
bool vtkSlicerReportingModuleLogic::IsInReport(vtkMRMLNode *node)
{
  if (!node)
    {
    return false;
    }
  std::string reportID = this->GetActiveReportID();
  if (reportID.compare("") == 0)
    {
    // no active report
    return false;
    }
  vtkMRMLReportingReportNode *reportNode = NULL;
  vtkMRMLNode *mrmlNode = this->GetMRMLScene()->GetNodeByID(reportID.c_str());
  if (!mrmlNode)
    {
    // no report node
    return false;
    }
  reportNode = vtkMRMLReportingReportNode::SafeDownCast(mrmlNode);
  if (!reportNode)
    {
    // no valid report node
    return false;
    }
  const char *volumeID = this->GetVolumeIDForReportNode(reportNode);
  if (!volumeID)
    {
    // no volume to be associated with
    return false;
    }
  const char *reportAttribute = node->GetAttribute("ReportingReportNodeID");
  if (!reportAttribute)
    {
    // not in a report
    return false;
    }
  if (reportID.compare(reportAttribute) != 0)
    {
    // in a different report
    return false;
    }
  const char *associatedVolumeNode = node->GetAttribute("AssociatedNodeID");
  if (!associatedVolumeNode)
    {
    // not associated with a volume
    return false;
    }
  if (strcmp(associatedVolumeNode, volumeID) != 0)
    {
    // associated with a different volume
    return false;
    }
  // otherwise it's in this report for this volume
  return true;
}

//---------------------------------------------------------------------------
const char *vtkSlicerReportingModuleLogic::GetVolumeIDForReportNode(vtkMRMLReportingReportNode *node)
{
  if (!node)
    {
    vtkErrorMacro("GetVolumeIDForReportNode: null report node");
    return NULL;
    }
  std::string volumeID = node->GetVolumeNodeID();
  if (volumeID.compare("") == 0)
    {
    return NULL;
    }
  else
    {
    return volumeID.c_str();
    }
}

//---------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::HideAnnotationsForOtherReports(vtkMRMLReportingReportNode *reportNode)
{
  if (!reportNode)
    {
    return;
    }
  // get all the annotation nodes, check their ReportingReportNodeID attribute
  // for this id

  // first, fiducials
  std::vector<vtkMRMLNode *> fiducialNodes;
  int numNodes = this->GetMRMLScene()->GetNodesByClass("vtkMRMLAnnotationFiducialNode", fiducialNodes);
  for (int i = 0; i < numNodes; i++)
    {
    vtkMRMLDisplayableNode *fidNode = vtkMRMLDisplayableNode::SafeDownCast(fiducialNodes[i]);
    if (fidNode)
      {
      const char *reportAttribute = fidNode->GetAttribute("ReportingReportNodeID");
      // if it doesn't have a report attribute set, or if it doesn't match
      // this report's id, hide it
      if (!reportAttribute ||
          strcmp(reportAttribute, reportNode->GetID()) != 0)
        {
        fidNode->SetDisplayVisibility(0);
        }
      else
        {
        fidNode->SetDisplayVisibility(1);
        }
      }
    }
  // and same for the ruler nodes
  std::vector<vtkMRMLNode *> rulerNodes;
  numNodes = this->GetMRMLScene()->GetNodesByClass("vtkMRMLAnnotationRulerNode", rulerNodes);
  for (int i = 0; i < numNodes; i++)
    {
    vtkMRMLDisplayableNode *rulerNode = vtkMRMLDisplayableNode::SafeDownCast(rulerNodes[i]);
    if (rulerNode)
      {
      const char *reportAttribute = rulerNode->GetAttribute("ReportingReportNodeID");
      // if it doesn't have a report attribute set, or if it doesn't match
      // this report's id, hide it
      if (!reportAttribute ||
          strcmp(reportAttribute, reportNode->GetID()) != 0)
        {
        rulerNode->SetDisplayVisibility(0);
        }
      else
        {
        rulerNode->SetDisplayVisibility(1);
        }
      }
    }
}

//---------------------------------------------------------------------------
int vtkSlicerReportingModuleLogic::SaveReportToAIM(vtkMRMLReportingReportNode *reportNode)
{
  if(!this->DICOMDatabase || !this->DICOMDatabase->isOpen())
    {
    vtkErrorMacro("SaveReportToAIM: DICOM database not initialized!");
    return EXIT_FAILURE;
    }

  if (!reportNode)
    {
    vtkErrorMacro("SaveReportToAIM: no report node given.");
    return EXIT_FAILURE;
    }
  
  char aimUID[128];
  dcmGenerateUniqueIdentifier(aimUID,SITE_SERIES_UID_ROOT);

  std::string dirname = reportNode->GetStorageDirectoryName();
  if (dirname == "")
    {
    vtkErrorMacro("SaveReportToAIM: no directory name given.");
    return EXIT_FAILURE;
    }

  vtkDebugMacro("SaveReportToAIM: directory name = " << dirname);

  vtkMRMLScalarVolumeNode *volumeNode = NULL;

  // only one volume is allowed for now, so get the active one
  const char *volumeID = this->GetVolumeIDForReportNode(reportNode);
  if (volumeID)
    {
    vtkMRMLNode *mrmlVolumeNode = this->GetMRMLScene()->GetNodeByID(volumeID);
    if (!mrmlVolumeNode)
      {
      vtkErrorMacro("SaveReportToAIM: volume node not found by id: " << volumeID);
      }
    else
      {
      volumeNode = vtkMRMLScalarVolumeNode::SafeDownCast(mrmlVolumeNode);
      }
    }

  // get some environment variables first
  std::string envUser, envHost, envHostName;
  vtksys::SystemTools::GetEnv("USER", envUser);
  vtksys::SystemTools::GetEnv("HOST", envHost);
  if (envHost.compare("")== 0)
    {
    vtksys::SystemTools::GetEnv("HOSTNAME", envHostName);
    }

  // PatientID
  QString patientID = this->DICOMDatabase->headerValue("0010,0020");
  if (patientID.size() == 0 ||
      patientID.contains("(no value available)"))
    {
    // not found, use a dummy string
    patientID = "NA";
    }
  else
    {
    patientID = patientID.split("]")[0].split("[")[1];
    vtkDebugMacro("Patient id = " << qPrintable(patientID) );
    }

  // PatientName
  QString patientName = this->DICOMDatabase->headerValue("0010,0010");
  if (patientName.size() == 0 ||
      patientName.contains("(no value available)"))
    {
    patientName = "NA";
    }
  else
    {
    patientName = patientName.split("]")[0].split("[")[1];
    vtkDebugMacro("patientName = " << qPrintable(patientName) );
    }

  // PatientSex
  QString patientSex = this->DICOMDatabase->headerValue("0010,0040");
  if (patientSex.size() == 0 ||
      patientSex.contains("(no value available)"))
    {
    patientSex = "M";
    }
  else
    {
    patientSex = patientSex.split("]")[0].split("[")[1];
    vtkDebugMacro("patientSex = " << qPrintable(patientSex) );
    }

  // open the file for writing
  
  // generated the document and parent elements
  //
  // (Step 1) Initialize ImageAnnotation and attributes

  // first get the current time
  struct tm * timeInfo;
  time_t rawtime;
  // yyyy/mm/dd-hh-mm-ss-ms-TZ
  // plus one for 3 char time zone
  char timeStr[27];
  time ( &rawtime );
  timeInfo = localtime (&rawtime);
  strftime (timeStr, 27, "%Y-%m-%dT%H:%M:%S", timeInfo);
  
  QDomDocument doc;
  QDomProcessingInstruction xmlDecl = doc.createProcessingInstruction("xml","version=\"1.0\"");
  doc.appendChild(xmlDecl);

  QDomElement root = doc.createElement("ImageAnnotation");
  root.setAttribute("xmlns","gme://caCORE.caCORE/3.2/edu.northwestern.radiology.AIM");
  root.setAttribute("aimVersion","3.0");
  root.setAttribute("cagridId","0");

  root.setAttribute("codeMeaning","na");
  root.setAttribute("codeValue", "na");
  root.setAttribute("codingSchemeDesignator", "na");
  root.setAttribute("dateTime",timeStr);

  vtkMRMLColorNode *colorNode = NULL;
  if (reportNode->GetColorNodeID().size() == 0)
    {
    vtkErrorMacro("No color node defined in report! Cannot get label description for finding.");
    return EXIT_FAILURE;
    }
  else
    {
    colorNode = vtkMRMLColorNode::SafeDownCast(this->GetMRMLScene()->GetNodeByID(reportNode->GetColorNodeID()));
    }
  if(!colorNode)
    {
    vtkErrorMacro("Failed to get label decription from color node " << reportNode->GetColorNodeID());
    return EXIT_FAILURE;
    }

  root.setAttribute("name", (std::string(qPrintable(patientID))+"_"+envUser+"_"+timeStr).c_str());
  root.setAttribute("uniqueIdentifier", aimUID);
  root.setAttribute("xmlns:xsi","http://www.w3.org/2001/XMLSchema-instance");
  root.setAttribute("xsi:schemaLocation","gme://caCORE.caCORE/3.2/edu.northwestern.radiology.AIM AIM_v3_rv11_XML.xsd");
  
  doc.appendChild(root);

  // (Step 3) Initialize user/equipment/person (these have no meaning for now
  // here)
  QDomElement user = doc.createElement("user");
  root.appendChild(user);
  QDomElement User = doc.createElement("User");
  User.setAttribute("cagridId","0");
  User.setAttribute("loginName",envUser.c_str());
  User.setAttribute("name",envUser.c_str());
  User.setAttribute("numberWithinRoleOfClinicalTrial","1");
  User.setAttribute("roleInTrial","Performing");
  user.appendChild(User);

  QDomElement equipment = doc.createElement("equipment");
  root.appendChild(equipment);
  QDomElement Equipment = doc.createElement("Equipment");
  Equipment.setAttribute("cagridId","0");
  Equipment.setAttribute("manufacturerModelName","3D_Slicer_4_Reporting");
  Equipment.setAttribute("manufacturerName","Brigham and Women's Hospital, Surgical Planning Lab");
  // get the slicer version
  qSlicerApplication* slicer = qSlicerApplication::application();
  QString slicerVersion = QString("Slicer ") + slicer->applicationVersion() + " r" + slicer->repositoryRevision();
  // set the Reporting module version, Id will give the git hash of the blob
  std::string reportingVersion = std::string(" Reporting ") + Reporting_WC_REVISION;
  std::string softwareVersion = std::string(qPrintable(slicerVersion)) + reportingVersion;
  Equipment.setAttribute("softwareVersion",softwareVersion.c_str());
  equipment.appendChild(Equipment);


  // (Step 2) Create inference collection and initialize each of the inference
  // objects based on the content of the annotation node
  //
  // Deprecated
  //
  // create anatomicEntityCollection that will describe what is being
  // annotated
  QDomElement aec = doc.createElement("anatomicEntityCollection");
  root.appendChild(aec);

  QDomElement ae = doc.createElement("AnatomicEntity");
  ae.setAttribute("annotatorConfidence", "0.0"); // TODO? add an option?
  ae.setAttribute("cagridId", "0");
  ae.setAttribute("codeMeaning", colorNode->GetColorName(reportNode->GetFindingLabel()));
  std::ostringstream labelValueStr;
  labelValueStr << reportNode->GetFindingLabel();
  ae.setAttribute("codeValue", labelValueStr.str().c_str());
  ae.setAttribute("codingSchemeDesignator", "3DSlicer"); // TODO use RadLex instead of Slicer
  ae.setAttribute("label", colorNode->GetColorName(reportNode->GetFindingLabel()));
  aec.appendChild(ae);

 
  if (reportNode)
    {
    std::cout << "SaveReportToAIM: saving report node " << reportNode->GetName() << std::endl;
    }
  
  // print out the volume
  if (volumeNode)
    {
    std::cout << "SaveReportToAIM: saving volume node " << volumeNode->GetName() << std::endl;
    }
  
  // print out the markups
  //   keep the list of referenced slice UIDs so that they can be saved in the
  //   final step
  QStringList allInstanceUIDs;
  int shapeId = 0;
  vtkSmartPointer<vtkCollection> labelNodeCollection = vtkSmartPointer<vtkCollection>::New();
  vtkSmartPointer<vtkCollection> annotationNodeCollection = vtkSmartPointer<vtkCollection>::New();

  // Find all label nodes in the report that are associated with the active
  // volume
  QStringList referencedUIDList;

  std::vector<vtkMRMLNode *> volumeNodes;
  int numNodes = this->GetMRMLScene()->GetNodesByClass("vtkMRMLScalarVolumeNode", volumeNodes);
  for (int i = 0; i < numNodes; i++)
    {
     vtkMRMLScalarVolumeNode *labelVolumeNode = vtkMRMLScalarVolumeNode::SafeDownCast(volumeNodes[i]);
     if (labelVolumeNode && labelVolumeNode->GetLabelMap())
       {
       // is this label map in the report and associated with the volume?
       if (this->IsInReport(labelVolumeNode))
         {
         std::cout << "Found label volume in report " << reportNode->GetID() << ", id = " << labelVolumeNode->GetID() << std::endl;
         labelNodeCollection->AddItem(labelVolumeNode);
         }
       }
    }
  // get the rulers and fiducials associated with this report and volume and
  // get the volume UIDs for them
  std::vector<vtkMRMLNode *> annotationNodes;
  int numAnnotationNodes =  this->GetMRMLScene()->GetNodesByClass("vtkMRMLAnnotationNode", annotationNodes);
  for (int i = 0; i < numAnnotationNodes; i++)
    {
    vtkMRMLAnnotationNode *annotationNode = vtkMRMLAnnotationNode::SafeDownCast(annotationNodes[i]);
    if (this->IsInReport(annotationNode))
      {
      // TODO: need to handle the case of multiframe data .. ?
      std::string sliceUID = this->GetSliceUIDFromMarkUp(annotationNode);
      if(sliceUID.compare("NONE") == 0)
        {
        vtkErrorMacro("Cannot save AIM report: volume being annotated, " << volumeNode->GetName()  << " is not a DICOM volume!");
        return EXIT_FAILURE;
        }
      std::cout << "Found associated annotation node " << annotationNode->GetID() << std::endl;
      annotationNodeCollection->AddItem(annotationNode);
      referencedUIDList << QString(sliceUID.c_str());
      allInstanceUIDs << QString(sliceUID.c_str());
      }
    }
  
  if(labelNodeCollection->GetNumberOfItems())
    {
    // save the SEG object, add it to the database, get the UIDs
    //   and initialize the corresponding element in AIM
    QSettings settings;
    std::string dbFileName = reportNode->GetDICOMDatabaseFileName();
    
    if(dbFileName != "")
      {
      // filename is initialized based on the UID of the segmentation object
      // that is generated by DicomSegWrite()
      std::string filename = this->DicomSegWrite(labelNodeCollection, dirname);
      if(filename != "")
        {
        const QString qfilename = QString(filename.c_str());
        this->DICOMDatabase->insert(qfilename, 0);

        DcmFileFormat segFileFormat;
        OFCondition status = segFileFormat.loadFile(filename.c_str());
        if(status.good())
          {
          DcmDataset *segDcm = segFileFormat.getAndRemoveDataset();
          QDomElement scDom = doc.createElement("segmentationCollection");
          root.appendChild(scDom);

          QDomElement segDom = doc.createElement("Segmentation");
          segDom.setAttribute("cagridId","0");

          std::string instanceUID = 
            this->getDcmElementAsString(DCM_SOPInstanceUID,segDcm).c_str();

          /*
          We should not reference directly in AIM the original data referenced from seg object, right?
          Then what should be the logic on loading DICOM SEG -- load referenced dataset automatically?
          allInstanceUIDs << QString(instanceUID.c_str());
          */

          segDom.setAttribute("sopInstanceUID", instanceUID.c_str());
          segDom.setAttribute("sopClassUID",
            this->getDcmElementAsString(DCM_SOPClassUID, segDcm).c_str());
          segDom.setAttribute("referencedSopInstanceUID",
            this->getDcmElementAsString(DCM_ReferencedSOPInstanceUID, segDcm).c_str());
          segDom.setAttribute("segmentNumber","1");
          scDom.appendChild(segDom);
          }
        else
          {
          std::cout << "Failed to load the created SEG object" << std::endl;
          }
        }
      else
        {
        std::cout << "DicomSegWrite() did not return a valid file name for the SEG object" << std::endl;
        }
      }
    else
      {
      std::cout << "DICOM DB path is not initialized, cannot create SEG object" << std::endl;
      }
    }

  // (Step 5) Iterate over referenced volume UIDs and add to the report
  // imageReferenceCollection
  //  +-ImageReference
  //     +-imageStudy
  //        +-ImageStudy (why do they have this nesting?)
  //           +-imageSeries
  //              +-ImageSeries
  //                 +-imageCollection
  //                    +-Image -- whooh ...

  // iterate over all instance UIDs and find all the series and corresponding
  // study/series UIDs referenced by the markups we have
  std::map<QString, QStringList> seriesToInstanceList;  // seriesUID to the list of corresponding instance UIDs
  std::map<QString, QStringList> studyToSeriesList;  // studyUID to the list of unique seriesUIDs
  std::map<QString, DcmDataset*> instanceToDcm; // instanceUID to DcmDataset
  for(int i=0;i<allInstanceUIDs.size();i++)
    {
    
    std::string fileName = this->GetFileNameFromUID(allInstanceUIDs[i].toLatin1().data());
    if(fileName == "")
      {
      vtkErrorMacro("Failed to get filename from uid " << i << ": " << qPrintable(allInstanceUIDs[i]));
      return EXIT_FAILURE;
      }
    DcmFileFormat fileFormat;
    OFCondition status = fileFormat.loadFile(fileName.c_str());
    if(!status.good())
      {
      vtkErrorMacro("Failed to load file " << fileName.c_str());
      return EXIT_FAILURE;
      }

    DcmDataset *dcm = fileFormat.getAndRemoveDataset();
    instanceToDcm[allInstanceUIDs[i]] = dcm;

    QString instanceUID = QString(this->getDcmElementAsString(DCM_SOPInstanceUID, dcm).c_str());
    QString studyUID = QString(this->getDcmElementAsString(DCM_StudyInstanceUID, dcm).c_str());
    QString seriesUID = QString(this->getDcmElementAsString(DCM_SeriesInstanceUID, dcm).c_str());

    std::cout << "instanceUID = " << instanceUID.toLatin1().data() << std::endl;
    std::cout << "studyUID = " << studyUID.toLatin1().data() << std::endl;
    std::cout << "seriesUID = " << seriesUID.toLatin1().data() << std::endl;
          
    if(seriesToInstanceList.find(seriesUID) == seriesToInstanceList.end())
      {
      seriesToInstanceList[seriesUID] = QStringList() << instanceUID;
      }
    else
      {
      if(seriesToInstanceList[seriesUID].indexOf(instanceUID) == -1)
        {
        seriesToInstanceList[seriesUID] << instanceUID;
        }
      }
    if(studyToSeriesList.find(studyUID) == studyToSeriesList.end())
      {
      studyToSeriesList[studyUID] = QStringList() << seriesUID;
      }
    else
      {
      if(studyToSeriesList[studyUID].indexOf(seriesUID) == -1)
        {
        studyToSeriesList[studyUID] << seriesUID;
        }
      }
    }

  QDomElement irc = doc.createElement("imageReferenceCollection");
  root.appendChild(irc);

  for(std::map<QString,QStringList>::const_iterator mIt=studyToSeriesList.begin();
      mIt!=studyToSeriesList.end();++mIt)
    {

    QString studyUID = mIt->first;
    QStringList seriesUIDs = mIt->second;

    for(int ser=0;ser<seriesUIDs.size();++ser)
      {

      QString seriesUID = seriesUIDs[ser];

      // for each list, create a new ImageReference element
      QDomElement ir = doc.createElement("ImageReference");
      ir.setAttribute("cagridId","0");
      ir.setAttribute("xsi:type","DICOMImageReference");
      irc.appendChild(ir);

      QDomElement study = doc.createElement("imageStudy");
      ir.appendChild(study);

      QDomElement study1 = doc.createElement("ImageStudy");
      study1.setAttribute("cagridId","0");
      study1.setAttribute("instanceUID",studyUID.toLatin1().data());
      study1.setAttribute("startDate","2000-01-01T00:00:00");
      study1.setAttribute("startTime","000000");
      study.appendChild(study1);

      // 
      QDomElement series = doc.createElement("imageSeries");
      study1.appendChild(series);

      QDomElement series1 = doc.createElement("ImageSeries");
      series1.setAttribute("cagridId","0");
      series1.setAttribute("instanceUID",seriesUID.toLatin1().data());
      series.appendChild(series1);

      QDomElement ic = doc.createElement("imageCollection");
      series1.appendChild(ic);

      QStringList uidList = seriesToInstanceList[seriesUID];

      for(int i=0;i<uidList.size();i++)
        {
        QDomElement image = doc.createElement("Image");
        image.setAttribute("cagridId","0");
        DcmDataset *dcm = instanceToDcm[uidList[i].toLatin1().data()];
        std::string classUID = this->getDcmElementAsString(DCM_SOPClassUID, dcm);
        image.setAttribute("sopClassUID", QString(classUID.c_str()));
        image.setAttribute("sopInstanceUID",uidList[i]);
        ic.appendChild(image);
        }
      }
    }

  // (Step 4) Go over the markup elements and add them to the geometric shape
  // collection. Here we might want to keep track of the volume being
  // referenced, but since one AIM file is for one volume, we don't really
  // need to do this.
  QDomElement gsc = doc.createElement("geometricShapeCollection");
  root.appendChild(gsc);

  // get the associated markups and print them
  for (int i = 0; i < annotationNodeCollection->GetNumberOfItems(); ++i)
    {
    vtkMRMLNode *mrmlAssociatedNode = vtkMRMLNode::SafeDownCast(annotationNodeCollection->GetItemAsObject(i));
    if (mrmlAssociatedNode)
      {
      vtkMRMLAnnotationNode *annNode = vtkMRMLAnnotationNode::SafeDownCast(mrmlAssociatedNode);
      vtkMRMLAnnotationFiducialNode *fidNode = vtkMRMLAnnotationFiducialNode::SafeDownCast(mrmlAssociatedNode);          
      vtkMRMLAnnotationRulerNode *rulerNode = vtkMRMLAnnotationRulerNode::SafeDownCast(mrmlAssociatedNode);
      // print out a point
      if(fidNode || rulerNode)
        {
        QStringList coordStr = this->GetMarkupPointCoordinatesStr(annNode);
        
        QDomElement gs = doc.createElement("GeometricShape");
        
        // GeometricShape markup-specific initialization
        
        // Fiducial = AIM Point
        if (fidNode)
          {
          vtkDebugMacro("SaveReportToAIM: saving Point from node named " << fidNode->GetName());
          
          if(coordStr.size()!=2)
            {
            vtkErrorMacro("Failed to obtain fiducial points for markup point!");
            return EXIT_FAILURE;
            }
          
          gs.setAttribute("xsi:type","Point");
          gs.setAttribute("shapeIdentifier",shapeId++);
          gs.setAttribute("includeFlag", "true");
          gs.setAttribute("cagridId","0");
          }
        
        // Ruler = AIM MultiPoint
        if (rulerNode)
          {
          vtkDebugMacro("SaveReportToAIM: saving MultiPoint from node named " << rulerNode->GetName());
          
          if(coordStr.size()!=4)
            {
            vtkErrorMacro("Failed to obtain fiducial points for markup point!");
            return EXIT_FAILURE;
            }
          
          gs.setAttribute("xsi:type","MultiPoint");
          gs.setAttribute("shapeIdentifier",shapeId++);
          gs.setAttribute("includeFlag", "true");
          gs.setAttribute("cagridId","0");
          }
        
        // Procedure for saving the list of points should be the same for
        // all markup elements
        this->AddSpatialCoordinateCollectionElement(doc, gs, coordStr, referencedUIDList[i]);
        gsc.appendChild(gs);
        }
      else
        {
        vtkWarningMacro("SaveReportToAIM: unsupported markup type, of class: " << mrmlAssociatedNode->GetClassName());
        }
      }
    }
  
  // Extract patient information from the referenced volume
  //
  QDomElement person = doc.createElement("person");
  root.appendChild(person);

  QDomElement Person = doc.createElement("Person");

  // load up the header information using the volume's first uid
  QString uids, volumeFirstUID;
  if (volumeNode)
    {
    uids = volumeNode->GetAttribute("DICOM.instanceUIDs");
    volumeFirstUID = uids.split(" ")[0];
    }
  vtkDebugMacro("Loading instance header from uid " << qPrintable(volumeFirstUID));
  this->DICOMDatabase->loadInstanceHeader(volumeFirstUID);

  // PatientBirthDate and PatientBirthTime
  QString patientBirthDate = this->DICOMDatabase->headerValue("0010,0030");
  if (patientBirthDate.size() == 0 ||
      patientBirthDate.contains("(no value available)"))
    {
    Person.setAttribute("birthDate","1990-01-01T00:00:00");
    }
  else
    {
    patientBirthDate = patientBirthDate.split("]")[0].split("[")[1];
    vtkDebugMacro("patientBirthDate = " << qPrintable(patientBirthDate));
    // parse birth date out from YYYYMMDD
    QString fullBirthDate = patientBirthDate.mid(0,4) + QString("-") + patientBirthDate.mid(4,2) + QString("-") + patientBirthDate.mid(6,2);
    QString patientBirthTime = this->DICOMDatabase->headerValue("0010,0032");
    QString fullBirthTime = QString("T00:00:00");
    if (patientBirthTime.size() != 0 &&
        !patientBirthTime.contains("(no value available)"))
      {      
      patientBirthTime = patientBirthTime.split("]")[0].split("[")[1];
      vtkDebugMacro("patientBirthTime = " << qPrintable(patientBirthTime) );
      if (patientBirthTime.size() >= 6)
        {
        // parse from HHMMSS
        fullBirthTime = patientBirthTime.mid(0,2) + QString("-") + patientBirthTime.mid(2,2) + QString("-") + patientBirthTime.mid(4,2);
        }
      }
    Person.setAttribute("birthDate", fullBirthDate + fullBirthTime);
    }

  Person.setAttribute("cagridId","0");
  Person.setAttribute("id",patientID);
  Person.setAttribute("name", patientName);
  Person.setAttribute("sex", patientSex);
  person.appendChild(Person);
 
  // close the file
  
  vtkDebugMacro("Here comes the AIM: ");
  QString xml = doc.toString();
  std::cout << qPrintable(xml);

  std::string aimFileName = dirname + "/" + aimUID + ".xml";
  reportNode->SetAIMFileName(aimFileName);
  
  std::ofstream outputFile(aimFileName.c_str());
  outputFile << qPrintable(xml);

  return EXIT_SUCCESS;
    
}

//---------------------------------------------------------------------------
int vtkSlicerReportingModuleLogic::AddSpatialCoordinateCollectionElement(QDomDocument &doc, QDomElement &parent,
                                                                         QStringList &coordList, QString &sliceUID)
{
  QDomElement fidscC = doc.createElement("spatialCoordinateCollection");
  parent.appendChild(fidscC);

  // All points should have the same slice UID, because coordinates are
  // defined on the slice
  //if(coordList.size()/2 != sliceUIDList.size())
  //  return EXIT_FAILURE;

  for(int i=0;i<coordList.size();i+=2)
    {
    QDomElement sc = doc.createElement("SpatialCoordinate");
    fidscC.appendChild(sc);

    sc.setAttribute("cagridId","0");
    sc.setAttribute("coordinateIndex","0");
    sc.setAttribute("imageReferenceUID",sliceUID);
    sc.setAttribute("referencedFrameNumber","1"); // TODO: maybe add handling of multiframe DICOM?
    sc.setAttribute("xsi:type", "TwoDimensionSpatialCoordinate");
    sc.setAttribute("x", coordList[i]);
    sc.setAttribute("y", coordList[i+1]);
    }

  return EXIT_SUCCESS;
}

//---------------------------------------------------------------------------
vtkMRMLScalarVolumeNode* vtkSlicerReportingModuleLogic::GetMarkupVolumeNode(vtkMRMLAnnotationNode *node)
{
  if (!node)
    {
    vtkErrorMacro("GetMarkupVolumeNode: no input node!");
    return  0;
    }

  if (!this->GetMRMLScene())
    {
    vtkErrorMacro("GetMarkupVolumeNode: No MRML Scene defined!");
    return 0;
    }

  vtkMRMLAnnotationControlPointsNode *cpNode = vtkMRMLAnnotationControlPointsNode::SafeDownCast(node);
  if (!node)
    {
    vtkErrorMacro("GetMarkupVolumeNode: Input node is not a control points node!");
    return 0;
    }

  int numPoints = cpNode->GetNumberOfControlPoints();
  vtkDebugMacro("GetMarkupVolumeNode: have a control points node with " << numPoints << " points");
  if(!numPoints)
    {
    vtkErrorMacro("GetMarkupVolumeNode: Input node has 0 control points!");
    return 0;
    }

  // get the associated node
  const char *associatedNodeID = cpNode->GetAttribute("AssociatedNodeID");
  if (!associatedNodeID)
    {
    vtkErrorMacro("GetMarkupVolumeNode: No AssociatedNodeID on the annotation node");
    return 0;
    }
  vtkMRMLScalarVolumeNode *volumeNode = NULL;
  vtkMRMLNode *mrmlNode = this->GetMRMLScene()->GetNodeByID(associatedNodeID);
  if (!mrmlNode)
    {
    vtkErrorMacro("GetMarkupVolumeNode: Associated node not found by id: " << associatedNodeID);
    return 0;
    }
  volumeNode = vtkMRMLScalarVolumeNode::SafeDownCast(mrmlNode);
  if (!volumeNode)
    {
    vtkErrorMacro("GetMarkupVolumeNode: Associated node with id: " << associatedNodeID << " is not a volume node!");
    return 0;
    }
  vtkDebugMacro("GetMarkupVolumeNode: Associated volume node ID: " << volumeNode->GetID());
  if (this->GetDebug())
    {
    vtkIndent ind;
    volumeNode->PrintSelf(std::cout,ind);
    }
  return volumeNode;
}

//---------------------------------------------------------------------------
QStringList vtkSlicerReportingModuleLogic::GetMarkupPointCoordinatesStr(vtkMRMLAnnotationNode *ann)
{
  QStringList sl;
  vtkMRMLAnnotationControlPointsNode *cpNode = vtkMRMLAnnotationControlPointsNode::SafeDownCast(ann);
  if (!cpNode)
    {
    vtkErrorMacro("GetMarkupPointCoordinatesStr: Input node is not a control points node!");
    return sl;
    }

  int numPoints = cpNode->GetNumberOfControlPoints();

  vtkMRMLScalarVolumeNode *vol = this->GetMarkupVolumeNode(ann);
  if(!vol)
    {
    vtkErrorMacro("Failed to obtain volume pointer!");
    return sl;
    }
  vtkSmartPointer<vtkMatrix4x4> ras2ijk = vtkSmartPointer<vtkMatrix4x4>::New();
  vol->GetRASToIJKMatrix(ras2ijk);

  for(int i=0;i<numPoints;i++)
  {
    double ras[4] = {0.0, 0.0, 0.0, 1.0};
    cpNode->GetControlPointWorldCoordinates(i, ras);
    std::cout << "RAS = " << ras[0] << ", " << ras[1] << ", " << ras[2] << std::endl;
    // convert point from ras to ijk
    double ijk[4] = {0.0, 0.0, 0.0, 1.0};
    ras2ijk->MultiplyPoint(ras, ijk);
    // TODO: may need special handling, because this assumes IS acquisition direction
    std::ostringstream ss1, ss2;

    ss1 << ijk[0];
    sl << QString(ss1.str().c_str());
    std::cout << "Coordinate: " << ss1.str().c_str() << std::endl;
    ss2 << ijk[1];
    sl << QString(ss2.str().c_str());
    std::cout << "Coordinate: " << ss2.str().c_str() << std::endl;

  }

  return sl;
}

//---------------------------------------------------------------------------
void vtkSlicerReportingModuleLogic::copyDcmElement(const DcmTag& tag, DcmDataset* dcmIn, DcmDataset* dcmOut)
{
  char *str;
  DcmElement* element;
  DcmTag copy = tag;
  std::cout << "Copying tag " << copy.getTagName() << std::endl;
  OFCondition cond = dcmIn->findAndGetElement(tag, element);
  if(cond.good())
  {
    element->getString(str);
    dcmOut->putAndInsertString(tag, str);
  }
  else
  {
    dcmOut->putAndInsertString(tag, "");
  }
}

//---------------------------------------------------------------------------
std::string vtkSlicerReportingModuleLogic::GetFileNameFromUID(const std::string uid)
{
  std::string fileName = "";
  if(this->DICOMDatabase)
    {
    QString qstr = this->DICOMDatabase->fileForInstance(uid.c_str());
    fileName = qstr.toLatin1().data();
    }
  return fileName;
}

//---------------------------------------------------------------------------
std::string vtkSlicerReportingModuleLogic::getDcmElementAsString(const DcmTag& tag, DcmDataset* dcmIn)
{
  char *str;
  DcmElement* element;
  DcmTag copy = tag;
  std::cout << "Copying tag " << copy.getTagName() << std::endl;
  OFCondition cond = dcmIn->findAndGetElement(tag, element);
  if(cond.good())
  {
    element->getString(str);
    return std::string(str);
  }
  return "";
}

//---------------------------------------------------------------------------
std::string vtkSlicerReportingModuleLogic::GetActiveReportID()
{
  std::string reportID = "";
  if (this->GetActiveParameterNodeID() == NULL)
    {
    vtkDebugMacro("GetActiveReportID: no active parameter node id, returning null");
    return reportID;
    }
  vtkMRMLScriptedModuleNode *parameterNode;
  vtkMRMLNode *mrmlNode = this->GetMRMLScene()->GetNodeByID(this->GetActiveParameterNodeID());
  if (!mrmlNode)
    {
    vtkErrorMacro("GetActiveReportID: no node with id " << this->GetActiveParameterNodeID());
    return reportID;
    }
  parameterNode = vtkMRMLScriptedModuleNode::SafeDownCast(mrmlNode);
  if (!parameterNode)
    {
    vtkErrorMacro("GetActiveReportID: no active parameter node with id " << this->GetActiveParameterNodeID());
    return reportID;
    }

  reportID = parameterNode->GetParameter("reportID");
  return reportID;  
}

//---------------------------------------------------------------------------
bool vtkSlicerReportingModuleLogic::IsDicomSeg(const std::string fname)
{
   DcmFileFormat fileFormat;
   OFCondition status = fileFormat.loadFile(fname.c_str());
   if(status.good())
   {
     DcmDataset *dcmDataset = fileFormat.getAndRemoveDataset();
     if(this->getDcmElementAsString(DCM_Modality, dcmDataset) == "SEG")
       return true;
   }
   return false;
}

//---------------------------------------------------------------------------
std::string vtkSlicerReportingModuleLogic::DicomSegWrite(vtkCollection* labelNodes, const std::string dirname, bool saveReferencedDcm)
{
  // TODO: what should be the behavior if the label node references a DICOM SEG already?
  // iterate over all labels:
  //   - check that the reference is the same
  //   - check that the reference has DICOM source
  unsigned numLabels = labelNodes->GetNumberOfItems();
  std::vector<std::string> refDcmSeriesUIDs;
  std::vector<vtkImageData*> labelImages;
  std::string referenceNodeID;
  vtkMRMLColorNode *colorNode = NULL;

  for (unsigned i=0;i<numLabels;i++)
    {
    vtkSmartPointer<vtkMRMLScalarVolumeNode> labelNode = vtkMRMLScalarVolumeNode::SafeDownCast(labelNodes->GetItemAsObject(i));
    if (!labelNode)
      {
      std::cout << "Expected label map" << std::endl;
      return "";
      }
    labelImages.push_back(labelNode->GetImageData());

    if (i==0)
      {
      if (!labelNode->GetDisplayNode() || !labelNode->GetDisplayNode()->GetColorNode())
        {
        std::cerr << "Label cannot be saved when the display node or color node is empty!" << std::endl;
        return "";
        }

      colorNode = labelNode->GetDisplayNode()->GetColorNode();

      referenceNodeID = labelNode->GetAttribute("AssociatedNodeID");
      if (referenceNodeID == "")
        {
        std::cerr << "Label cannot be saved when AssociatedNodeID attrubute is not initialized!" << std::endl;
        return "";
        }

      vtkMRMLScalarVolumeNode* referenceNode =
                vtkMRMLScalarVolumeNode::SafeDownCast(this->GetMRMLScene()->GetNodeByID(referenceNodeID.c_str()));
      if (!referenceNode)
        {
        std::cerr << "Label node does not have AssociatedNodeID initialized!" << std::endl;
        return "";
        }
      
      const char* uids = referenceNode->GetAttribute("DICOM.instanceUIDs");
      if (!uids)
        {
        std::cerr << "Referenced node does not have DICOM.instanceUIDs initialized!" << std::endl;
        return "";
        }
      
      std::istringstream iss(uids);
      std::string word;
      std::cout << "Reference dicom UIDs: ";
      while(std::getline(iss, word, ' '))
        {
        refDcmSeriesUIDs.push_back(word);
        std::cout << word << " ";
        }
      std::cout << std::endl;
      }
    else
      {
      std::string thisReferenceNodeID = labelNode->GetAttribute("AssociatedNodeID");
      if (thisReferenceNodeID != referenceNodeID)
        {
        std::cerr << "Label number " << i << " AssociatedNodeID = " << thisReferenceNodeID <<
          ", while the first label references " << referenceNodeID << std::endl;
        return "";
        }
      }
    }

  // pixel data
  //
  char buf[16] = {0};
  int extent[6];
  labelImages[0]->GetExtent(extent);
  std::cout << "Preparing pixel data" << std::endl;
  int nbytes = (int) (float((extent[1]+1)*(extent[3]+1)*(extent[5]+1))/8.);
  int total = 0;
  int labelValue = 0;

  unsigned char *pixelArray = new unsigned char[nbytes];
  vtkImageData *labelImage = labelImages[0];
  if(labelImage->GetScalarType() != 4)
    {
    vtkImageCast *cast = vtkImageCast::New();
    cast->SetInput(labelImage);
    cast->SetOutputScalarTypeToShort();
    cast->Update();
    vtkSmartPointer<vtkMRMLScalarVolumeNode> labelNode = vtkMRMLScalarVolumeNode::SafeDownCast(labelNodes->GetItemAsObject(0));
    labelNode->SetAndObserveImageData(cast->GetOutput());
    labelImage = cast->GetOutput();
    cast->Delete();
    }

  short* bufferPointer = (short*) labelImage->GetPointData()->GetScalars()->GetVoidPointer(0);
  vtkIdType inc[3];
  labelImage->GetIncrements(inc);

  for(int i=0;i<nbytes;i++)
    pixelArray[i] = 0;

  for(int k=0;k<extent[5]+1;k++)
    {
    for(int j=0;j<extent[3]+1;j++)
      {
      for(int i=0;i<extent[1]+1;i++)
        {
        int byte = total / 8, bit = total % 8;
        //int imageValue = labelImages[0]->GetScalarComponentAsFloat(i,j,k,0);
        // Optimized access to the image buffer:
        short imageValue = bufferPointer[k*inc[2]+j*inc[1]+i];
        if(imageValue)
          labelValue = imageValue;
        total++;
        pixelArray[byte] |= (imageValue ? 1 : 0) << bit;
        }
      }
    }


  // load the dcm datasets for the reference volume slices
  std::vector<DcmDataset*> dcmDatasetVector;
  for(std::vector<std::string>::const_iterator uidIt=refDcmSeriesUIDs.begin();
    uidIt!=refDcmSeriesUIDs.end();++uidIt)
    {
    std::string fileName = this->GetFileNameFromUID(*uidIt);
    if(fileName == "")
      {
      std::cout << "Failed to get filename from UID " << *uidIt << std::endl;
      return "";
      }
    DcmFileFormat fileFormat;
    OFCondition status = fileFormat.loadFile(fileName.c_str());
    if(status.good())
      {
      std::cout << "Loaded dataset for " << fileName << std::endl;
      dcmDatasetVector.push_back(fileFormat.getAndRemoveDataset());
      }
      else
      {
      std::cerr << "Failed to query the database! Exiting." << std::endl;
      return "";
      }
      if(saveReferencedDcm)
        {
        // save the referenced file into the export directory
        std::string outputFilename = dirname+"/"+*uidIt+".dcm";
        status = fileFormat.saveFile(outputFilename.c_str(), EXS_LittleEndianExplicit);
        if(status.bad())
          {
          std::cout << "Failed to export one of the referenced files: " << *uidIt << std::endl;
          }
        }
    }

  std::stringstream labelValueStr;
  labelValueStr << int(labelValue);
  


  // create a DICOM dataset (see
  // http://support.dcmtk.org/docs/mod_dcmdata.html#Examples)
  DcmDataset *dcm0 = dcmDatasetVector[0];

  DcmFileFormat fileformatOut;
  DcmDataset *dataset = fileformatOut.getDataset();

  // Patient IE
  //  Patient module
  copyDcmElement(DCM_PatientName, dcm0, dataset);
  copyDcmElement(DCM_PatientID, dcm0, dataset);
  copyDcmElement(DCM_PatientBirthDate, dcm0, dataset);
  copyDcmElement(DCM_PatientSex, dcm0, dataset);

  // Study IE
  //  General Study module
  copyDcmElement(DCM_StudyInstanceUID, dcm0, dataset);

  copyDcmElement(DCM_ReferringPhysicianName, dcm0, dataset);
  copyDcmElement(DCM_StudyID, dcm0, dataset);
  dataset->putAndInsertString(DCM_StudyID, "1"); // David Clunie: should be initialized (not required, but good idea)
  copyDcmElement(DCM_AccessionNumber, dcm0, dataset);

  OFString contentDate, contentTime;    // David Clunie: must be present and initialized
  DcmDate::getCurrentDate(contentDate);
  DcmTime::getCurrentTime(contentTime);
  dataset->putAndInsertString(DCM_ContentDate, contentDate.c_str());
  dataset->putAndInsertString(DCM_ContentTime, contentTime.c_str());
  dataset->putAndInsertString(DCM_SeriesDate, contentDate.c_str());
  dataset->putAndInsertString(DCM_SeriesTime, contentTime.c_str());
  dataset->putAndInsertString(DCM_StudyDate, contentDate.c_str());
  dataset->putAndInsertString(DCM_StudyTime, contentTime.c_str());
  dataset->putAndInsertString(DCM_AcquisitionDate, contentDate.c_str());
  dataset->putAndInsertString(DCM_AcquisitionTime, contentTime.c_str());

  // Series IE
  //  General Series module
  dataset->putAndInsertString(DCM_Modality,"SEG");
  char uid[128];
  char *seriesUIDStr = dcmGenerateUniqueIdentifier(uid, SITE_SERIES_UID_ROOT);
  dataset->putAndInsertString(DCM_SeriesInstanceUID,seriesUIDStr);

  //  Segmentation Series module
  dataset->putAndInsertString(DCM_SeriesNumber,"1000");

  // Frame Of Reference IE
  dataset->putAndInsertString(DCM_FrameOfReferenceUID, seriesUIDStr);
  dataset->putAndInsertString(DCM_PositionReferenceIndicator, ""); // David Clunie: must be present, may be empty

  // Equipment IE
  //  General Equipment module
  dataset->putAndInsertString(DCM_Manufacturer, "3D Slicer Community");

  //  Enhanced General Equipment module
  dataset->putAndInsertString(DCM_ManufacturerModelName, "AndreyTestDICOMSegWriter v.0.0.1");
  dataset->putAndInsertString(DCM_DeviceSerialNumber, "0.0.1");
  dataset->putAndInsertString(DCM_SoftwareVersions, "0.0.1");

  // Segmentation IE

  dataset->putAndInsertString(DCM_InstanceNumber,"1");
  dataset->putAndInsertString(DCM_InstanceNumber,"1");
  dataset->putAndInsertUint16(DCM_FileMetaInformationVersion,0x0001);
  dataset->putAndInsertString(DCM_SOPClassUID, UID_SegmentationStorage);

  //  General Image module
  dataset->putAndInsertString(DCM_InstanceNumber, "1");
  dataset->putAndInsertString(DCM_SOPInstanceUID, dcmGenerateUniqueIdentifier(uid, SITE_INSTANCE_UID_ROOT));
  dataset->putAndInsertString(DCM_ImageType,"DERIVED\\PRIMARY");

  dataset->putAndInsertString(DCM_InstanceCreatorUID,OFFIS_UID_ROOT);

  //  Image Pixel module
  dataset->putAndInsertString(DCM_SamplesPerPixel,"1");
  dataset->putAndInsertString(DCM_PhotometricInterpretation,"MONOCHROME2");

  sprintf(buf,"%d", extent[1]+1);
  dataset->putAndInsertString(DCM_Columns,buf);
  sprintf(buf,"%d", extent[3]+1);
  dataset->putAndInsertString(DCM_Rows,buf);

  dataset->putAndInsertString(DCM_BitsAllocated,"1"); // XIP: 8
  dataset->putAndInsertString(DCM_BitsStored,"1"); // XIP: 8
  dataset->putAndInsertString(DCM_HighBit,"0");
  dataset->putAndInsertString(DCM_PixelRepresentation,"0");

  sprintf(buf,"%d", extent[5]+1);
  dataset->putAndInsertString(DCM_NumberOfFrames,buf);

  dataset->putAndInsertString(DCM_LossyImageCompression,"00");

  copyDcmElement(DCM_PixelSpacing, dcm0, dataset);
  copyDcmElement(DCM_SliceThickness, dcm0, dataset);

  //   Segmentation Image module
  dataset->putAndInsertString(DCM_SegmentationType, "BINARY");
  dataset->putAndInsertString(DCM_ContentLabel, "ROI"); // CS
  dataset->putAndInsertString(DCM_ContentDescription, "3D Slicer segmentation result");
  dataset->putAndInsertString(DCM_SeriesDescription, "3D Slicer segmentation result");
  dataset->putAndInsertString(DCM_ContentCreatorName, "3DSlicer");

  // segment sequence [0062,0002]
  DcmItem *Item = NULL, *subItem = NULL, *subItem2 = NULL, *subItem3 = NULL, *SegmentSequenceItem = NULL;
  dataset->findOrCreateSequenceItem(DCM_SegmentSequence, SegmentSequenceItem);

  // AF TODO: go over all labels and insert separate item for each one
  SegmentSequenceItem->putAndInsertString(DCM_SegmentNumber, "1");
  SegmentSequenceItem->putAndInsertString(DCM_SegmentLabel, "Segmentation"); // AF TODO: this should be initialized based on the label value!
  SegmentSequenceItem->putAndInsertString(DCM_SegmentAlgorithmType, "SEMIAUTOMATIC");
  SegmentSequenceItem->putAndInsertString(DCM_SegmentAlgorithmName, "Editor");

  // general anatomy mandatory macro
  SegmentSequenceItem->findOrCreateSequenceItem(DCM_AnatomicRegionSequence, subItem);
  subItem->putAndInsertString(DCM_CodeValue, labelValueStr.str().c_str());
  subItem->putAndInsertString(DCM_CodingSchemeDesignator,"3DSlicer");
  subItem->putAndInsertString(DCM_CodeMeaning, colorNode->GetColorName(labelValue));

  //segmentation properties - category
  SegmentSequenceItem->findOrCreateSequenceItem(DCM_SegmentedPropertyCategoryCodeSequence, subItem);
  subItem->putAndInsertString(DCM_CodeValue,"T-D0050");
  subItem->putAndInsertString(DCM_CodingSchemeDesignator,"SRT");
  subItem->putAndInsertString(DCM_CodeMeaning,"Tissue");

  //segmentation properties - type
  SegmentSequenceItem->findOrCreateSequenceItem(DCM_SegmentedPropertyTypeCodeSequence, subItem);
  subItem->putAndInsertString(DCM_CodeValue,"M-03010");
  subItem->putAndInsertString(DCM_CodingSchemeDesignator,"SRT");
  subItem->putAndInsertString(DCM_CodeMeaning,"Nodule");

  //  Multi-frame Functional Groups Module
  //   Shared functional groups sequence
  std::cout << "Before initializing SharedFunctionalGroupsSequence" << std::endl;
  dataset->findOrCreateSequenceItem(DCM_SharedFunctionalGroupsSequence, Item);
  Item->findOrCreateSequenceItem(DCM_DerivationImageSequence, subItem);
  const unsigned long itemNum = extent[5];
  if(itemNum+1 != dcmDatasetVector.size())
    {
    std::cerr << "Number of slices " << extent[5] << " does not match the number of DcmDatasets " 
      << dcmDatasetVector.size() << "!" << std::endl;
    return "";
    }

  for(unsigned i=0;i<itemNum+1;i++)
    {

    subItem->findOrCreateSequenceItem(DCM_SourceImageSequence, subItem2, i);
    char *str;
    DcmElement *element;
    DcmDataset *dataset = dcmDatasetVector[i];
    dataset->findAndGetElement(DCM_SOPClassUID, element, i);
    element->getString(str);
    subItem2->putAndInsertString(DCM_ReferencedSOPClassUID, str);
    dataset->findAndGetElement(DCM_SOPInstanceUID, element);
    element->getString(str);
    subItem2->putAndInsertString(DCM_ReferencedSOPInstanceUID, str);
    subItem2->putAndInsertString(DCM_ReferencedFrameNumber, "1");

    subItem2->findOrCreateSequenceItem(DCM_PurposeOfReferenceCodeSequence, subItem3);
    subItem3->putAndInsertString(DCM_CodeValue, "121322");
    subItem3->putAndInsertString(DCM_CodingSchemeDesignator, "DCM");
    subItem3->putAndInsertString(DCM_CodeMeaning, "Source image for image processing operation");

    }


  std::cout << "Before initializing DerivationCodeSequence" << std::endl;
  subItem->findOrCreateSequenceItem(DCM_DerivationCodeSequence, subItem2);
  subItem2->putAndInsertString(DCM_CodeValue, "113076");
  subItem2->putAndInsertString(DCM_CodingSchemeDesignator, "DCM");
  subItem2->putAndInsertString(DCM_CodeMeaning, "Segmentation");

  char pixelSpacingStr[16*2+1], sliceThicknessStr[16];
  {
    char *str;
    DcmElement *element;
    dcmDatasetVector[0]->findAndGetElement(DCM_PixelSpacing, element);
    element->getString(str);
    strcpy(&pixelSpacingStr[0], str);
    dcmDatasetVector[0]->findAndGetElement(DCM_SliceThickness, element);
    element->getString(str);
    strcpy(&sliceThicknessStr[0], str);
   }
    {
    // Elements identical for each frame should be in shared group
    char *str;
    DcmElement *element;
    dcmDatasetVector[0]->findAndGetElement(DCM_ImageOrientationPatient, element);
    element->getString(str);
    Item->findOrCreateSequenceItem(DCM_PlaneOrientationSequence, subItem);
    subItem->putAndInsertString(DCM_ImageOrientationPatient, str);

    Item->findOrCreateSequenceItem(DCM_PixelMeasuresSequence, subItem);
    subItem->putAndInsertString(DCM_SliceThickness, sliceThicknessStr);
    subItem->putAndInsertString(DCM_PixelSpacing, pixelSpacingStr);
    }

  std::cout << "Before initializing PerFrameGroupsSequence" << std::endl;
  //Derivation Image functional group
  for(unsigned i=0;i<itemNum+1;i++)
    {
    dataset->findOrCreateSequenceItem(DCM_PerFrameFunctionalGroupsSequence, Item, i);

    char buf[64], *str;
    DcmElement *element;

    Item->findOrCreateSequenceItem(DCM_FrameContentSequence, subItem);
    subItem->putAndInsertString(DCM_StackID, "1");
    sprintf(buf, "%d", i+1);
    subItem->putAndInsertString(DCM_InStackPositionNumber, buf);
    sprintf(buf, "1\\%d", i+1);
    subItem->putAndInsertString(DCM_DimensionIndexValues, buf);

    dcmDatasetVector[i]->findAndGetElement(DCM_ImagePositionPatient, element);
    element->getString(str);
    Item->findOrCreateSequenceItem(DCM_PlanePositionSequence, subItem);
    subItem->putAndInsertString(DCM_ImagePositionPatient, str);

    Item->findOrCreateSequenceItem(DCM_SegmentIdentificationSequence, subItem);
    subItem->putAndInsertString(DCM_ReferencedSegmentNumber, "1");

    }


  // Multi-frame Dimension module
    {
    std::cout << "Before initializing DimensionOrganizationSequence" << std::endl;
    dataset->findOrCreateSequenceItem(DCM_DimensionOrganizationSequence, Item);
    char dimensionuid[128];
    char *dimensionUIDStr = dcmGenerateUniqueIdentifier(dimensionuid, SITE_SERIES_UID_ROOT);
    Item->putAndInsertString(DCM_DimensionOrganizationUID, dimensionUIDStr);

    dataset->findOrCreateSequenceItem(DCM_DimensionIndexSequence, Item, 0);

    Item->putAndInsertString(DCM_DimensionOrganizationUID, dimensionUIDStr);

    DcmAttributeTag dimAttr(DCM_StackID);
    Uint16 *dimAttrArray = new Uint16[2];

    dimAttr.putTagVal(DCM_StackID);
    dimAttr.getUint16Array(dimAttrArray);
    Item->putAndInsertUint16Array(DCM_DimensionIndexPointer, dimAttrArray, 1);

    dimAttr.putTagVal(DCM_FrameContentSequence);
    dimAttr.getUint16Array(dimAttrArray);
    Item->putAndInsertUint16Array(DCM_FunctionalGroupPointer, dimAttrArray, 1);

    dataset->findOrCreateSequenceItem(DCM_DimensionIndexSequence, Item, 1);

    Item->putAndInsertString(DCM_DimensionOrganizationUID, dimensionUIDStr);

    dimAttr.putTagVal(DCM_InStackPositionNumber);
    dimAttr.getUint16Array(dimAttrArray);
    Item->putAndInsertUint16Array(DCM_DimensionIndexPointer, dimAttrArray, 1);

    dimAttr.putTagVal(DCM_FrameContentSequence);
    dimAttr.getUint16Array(dimAttrArray);
    Item->putAndInsertUint16Array(DCM_FunctionalGroupPointer, dimAttrArray, 1);

    //delete [] dimAttrArray;
    }


  std::cout << "Inserting PixelData" << std::endl;
  dataset->putAndInsertUint8Array(DCM_PixelData, pixelArray, nbytes);//write pixels

  std::cout << "DICOM SEG created" << std::endl;

  delete [] pixelArray;

  std::string filename = dirname+"/"+seriesUIDStr+".dcm";
  OFCondition status = fileformatOut.saveFile(filename.c_str(), EXS_LittleEndianExplicit);
  if(status.bad())
  {
    std::cout << "Error writing " << filename << ". Error: " << status.text() << std::endl;
    return "";
  }

  return filename;
}

/* Read DICOM SEG object and create label(s) that correspond to the segments it stores.
  Assume that the SEG object is in the DB; assume that the reference series is also in the DB (?)

  This function does not make any assumptions about whether the volume that corresponds to the reference
  is in the scene or not.

  Relies on the functionality that allows to reconstruct the volume geometry from the list of filenames.
*/
bool vtkSlicerReportingModuleLogic::DicomSegRead(vtkCollection* labelNodes, const std::string instanceUID, vtkMRMLColorNode *colorNode)
{
    if(!this->DICOMDatabase)
      {
      std::cerr << "DICOM DB is not initialized!" << std::endl;
      return false;
      }
    if(!this->DICOMDatabase->isOpen())
      {
      std::cerr << "DICOM DB is not open!" << std::endl;
      return false;
      }

    // query the filename for the seg object from the database
    // get the list of reference UIDs
    // read the volume geometry
    // initialize the volume pixel array
    // create new volume for each segment?
    std::cout << "DicomSegRead will attempt to read for instance UID " << instanceUID << std::endl;
    std::string segFileName = this->GetFileNameFromUID(instanceUID);

    if(!colorNode)
      colorNode = this->GetDefaultColorNode();

    if(segFileName == "")
      {
      std::cout << "Failed to get the filename from DB for " << instanceUID << std::endl;
      return false;
      }

    DcmFileFormat fileFormat;
    DcmDataset *segDataset = NULL;
    OFCondition status = fileFormat.loadFile(segFileName.c_str());
    if(status.good())
      {
      std::cout << "Loaded dataset for " << segFileName << std::endl;
      segDataset = fileFormat.getAndRemoveDataset();
      }
    else
      {
      std::cout << "Failed to load the dataset for " << instanceUID << std::endl;
      return false;
      }

    // No go if this is not a SEG modality
      {
      DcmElement *el;
      char* str;
      OFCondition status =
        segDataset->findAndGetElement(DCM_SOPClassUID, el);
      if(status.bad())
        {
        std::cout << "Failed to get class UID" << std::endl;
        return -1;
        }
      status = el->getString(str);
      if(status.bad())
        return -1;
      if(strcmp(str, UID_SegmentationStorage))
        {
        std::cerr << "Input DICOM should be a SEG object!" << std::endl;
        return -1;
        }
      }

    // Step 2: get the UIDs of the source sequence to initialize the geometry
    std::vector<std::string> referenceFramesUIDs;
    {
      DcmItem *item1, *item2, *item3;
      DcmElement *el;
      OFCondition status;
      char* str;
      status = segDataset->findAndGetSequenceItem(DCM_SharedFunctionalGroupsSequence, item1);
      if(status.bad())
        return -2;
      status = item1->findAndGetSequenceItem(DCM_DerivationImageSequence, item2);
      if(status.bad())
        return -2;
      //status = item2->findAndGetSequenceItem(DCM_SourceImageSequence, item3);
      // TODO: how to get the number of items in sequence?
      for(int i=0;;i++)
      {
        status = item2->findAndGetSequenceItem(DCM_SourceImageSequence, item3, i);
        if(status.bad())
          break;
        status = item3->findAndGetElement(DCM_ReferencedSOPInstanceUID, el);
        if(status.bad())
          return -3;
        status = el->getString(str);
        if(status.bad())
          return -4;
        std::cout << "Next UID: " << str << std::endl;
        referenceFramesUIDs.push_back(str);
      }
    }

  std::cout << referenceFramesUIDs.size() << " reference UIDs found" << std::endl;

  vtkSmartPointer<vtkMRMLVolumeArchetypeStorageNode> sNode = vtkSmartPointer<vtkMRMLVolumeArchetypeStorageNode>::New();
  sNode->ResetFileNameList();

  // TODO: initialize geometry directly from the frame information,
  //  no real need to read referenced series here
  for(std::vector<std::string>::const_iterator uidIt=referenceFramesUIDs.begin();
    uidIt!=referenceFramesUIDs.end();++uidIt)
    {
      std::string frameFileName = this->GetFileNameFromUID(*uidIt);
      if(uidIt == referenceFramesUIDs.begin())
        sNode->SetFileName(frameFileName.c_str());
      sNode->AddFileName(frameFileName.c_str());
    }
    sNode->SetSingleFile(0);

    vtkSmartPointer<vtkMRMLScalarVolumeNode> vNode = vtkSmartPointer<vtkMRMLScalarVolumeNode>::New();
    sNode->ReadData(vNode);

    // Step 4: Initialize the image
    const Uint8 *pixelArray;
      {
      unsigned long count;
      const DcmTagKey pdTag = DCM_PixelData;
      OFCondition status =
        segDataset->findAndGetUint8Array(pdTag, pixelArray, &count, false);
      if(!status.good())
        return -5;
      std::cout << "Pixel array length is " << count << std::endl;

      }

    // get the label value from SegmentSequence/AnatotmicRegionSequence
    DcmItem *Item = NULL, *subItem = NULL;
    char tagValue[128];
    const char* tagValuePtr = &tagValue[0]; 
    segDataset->findAndGetSequenceItem(DCM_SegmentSequence, Item);
    Item->findAndGetSequenceItem(DCM_AnatomicRegionSequence, subItem);

    subItem->findAndGetString(DCM_CodeValue, tagValuePtr);
    int tmpLabelValue = atoi(tagValuePtr);


    // label value is accepted only if the coding scheme designator is
    // recognized and the color name matches
    unsigned short labelValue = 1; 
    subItem->findAndGetString(DCM_CodingSchemeDesignator, tagValuePtr);    
    if(strcmp(tagValuePtr, "3DSlicer") != 0)
      {
      std::cerr << "WARNING: Coding scheme designator " << tagValuePtr << " is not recognized!" << std::endl;
      }
    else
      {
      subItem->findAndGetString(DCM_CodeMeaning, tagValuePtr);
      if(strcmp(tagValuePtr, colorNode->GetColorName(tmpLabelValue)) != 0)
        {
        std::cerr << "WARNING: Code meaning " << tagValuePtr << " does not match the expected value for label " <<
          tmpLabelValue << std::endl;
        }
      else
        {
        // use the tag value only if the coding scheme designator and color
        // name match!        
        labelValue = tmpLabelValue;
        }
      }

    vtkImageData *imageData = vNode->GetImageData();

    if(imageData->GetScalarType() != 4) // check if short
      {
      vtkImageCast *cast = vtkImageCast::New();
      cast->SetInput(imageData);
      cast->SetOutputScalarTypeToShort();
      cast->Update();
      vNode->SetAndObserveImageData(cast->GetOutput());
      imageData = cast->GetOutput();
      cast->Delete();
      }

    int extent[6];
    imageData->GetExtent(extent);

    short* bufferPointer = (short*) imageData->GetPointData()->GetScalars()->GetVoidPointer(0);
    vtkIdType inc[3];
    imageData->GetIncrements(inc);

    int total = 0;
    for(int k=0;k<extent[5]+1;k++)
      {
      for(int j=0;j<extent[3]+1;j++)
        {
        for(int i=0;i<extent[1]+1;i++)
          {
          int byte = total/8, bit = total % 8;
          int value = (pixelArray[byte] >> bit) & 1;
          value *= labelValue;
          //imageData->SetScalarComponentFromFloat(i,j,k,0,value);
          // Optimized access to the image buffer:
          bufferPointer[k*inc[2]+j*inc[1]+i] = value;
          total++;
          }
        }
      }

    vNode->LabelMapOn();
    vNode->SetAttribute("DICOM.instanceUIDs", instanceUID.c_str());

    std::string referenceInstanceUIDs;
    for(std::vector<std::string>::const_iterator uidIt=referenceFramesUIDs.begin();
      uidIt!=referenceFramesUIDs.end();++uidIt)
    {
      referenceInstanceUIDs += *uidIt + std::string(" ");
    }
 
    vNode->SetAttribute("DICOM.referenceInstanceUIDs", referenceInstanceUIDs.c_str());
    labelNodes->AddItem(vNode);

    return true;
}

//---------------------------------------------------------------------------
vtkMRMLColorNode* vtkSlicerReportingModuleLogic::GetDefaultColorNode()
{
  vtkMRMLColorNode* colorNode = 
    vtkMRMLColorNode::SafeDownCast(this->GetMRMLScene()->GetNodeByID("vtkMRMLColorTableNodeFileGenericAnatomyColors.txt"));
  if(colorNode && strcmp(colorNode->GetName(), "GenericAnatomyColors"))
    {
    colorNode = NULL;
    }
  return colorNode;
}
