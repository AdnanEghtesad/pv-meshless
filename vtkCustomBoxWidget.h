/*=========================================================================

  Program:   Visualization Toolkit
  Module:    $RCSfile: vtkCustomBoxWidget.h,v $

  Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
  All rights reserved.
  See Copyright.txt or http://www.kitware.com/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notice for more information.

=========================================================================*/
// .NAME vtkCustomBoxWidget - 3D widget for manipulating a box
// .SECTION Description
// This 3D widget interacts with a vtkCustomBoxRepresentation class (i.e., it
// handles the events that drive its corresponding representation). The
// representation is assumed to represent a region of interest that is
// represented by an arbitrarily oriented hexahedron (or box) with interior
// face angles of 90 degrees (i.e., orthogonal faces). The representation
// manifests seven handles that can be moused on and manipulated, plus the
// six faces can also be interacted with. The first six handles are placed on
// the six faces, the seventh is in the center of the box. In addition, a
// bounding box outline is shown, the "faces" of which can be selected for
// object rotation or scaling. A nice feature of vtkCustomBoxWidget, like any 3D
// widget, will work with the current interactor style. That is, if
// vtkCustomBoxWidget does not handle an event, then all other registered
// observers (including the interactor style) have an opportunity to process
// the event. Otherwise, the vtkBoxWidget will terminate the processing of
// the event that it handles.
//
// To use this widget, you generally pair it with a vtkCustomBoxRepresentation
// (or a subclass). Various options are available in the representation for 
// controlling how the widget appears, and how the widget functions.
//
// .SECTION Event Bindings
// By default, the widget responds to the following VTK events (i.e., it
// watches the vtkRenderWindowInteractor for these events):
// <pre>
// If one of the seven handles are selected:
//   LeftButtonPressEvent - select the appropriate handle 
//   LeftButtonReleaseEvent - release the currently selected handle 
//   MouseMoveEvent - move the handle
// If one of the faces is selected:
//   LeftButtonPressEvent - select a box face
//   LeftButtonReleaseEvent - release the box face
//   MouseMoveEvent - rotate the box
// In all the cases, independent of what is picked, the widget responds to the 
// following VTK events:
//   MiddleButtonPressEvent - translate the widget
//   MiddleButtonReleaseEvent - release the widget
//   RightButtonPressEvent - scale the widget's representation
//   RightButtonReleaseEvent - stop scaling the widget
//   MouseMoveEvent - scale (if right button) or move (if middle button) the widget
// </pre>
//
// Note that the event bindings described above can be changed using this
// class's vtkWidgetEventTranslator. This class translates VTK events 
// into the vtkCustomBoxWidget's widget events:
// <pre>
//   vtkWidgetEvent::Select -- some part of the widget has been selected
//   vtkWidgetEvent::EndSelect -- the selection process has completed
//   vtkWidgetEvent::Scale -- some part of the widget has been selected
//   vtkWidgetEvent::EndScale -- the selection process has completed
//   vtkWidgetEvent::Translate -- some part of the widget has been selected
//   vtkWidgetEvent::EndTranslate -- the selection process has completed
//   vtkWidgetEvent::Move -- a request for motion has been invoked
// </pre>
//
// In turn, when these widget events are processed, the vtkCustomBoxWidget
// invokes the following VTK events on itself (which observers can listen for):
// <pre>
//   vtkCommand::StartInteractionEvent (on vtkWidgetEvent::Select)
//   vtkCommand::EndInteractionEvent (on vtkWidgetEvent::EndSelect)
//   vtkCommand::InteractionEvent (on vtkWidgetEvent::Move)
// </pre>

// .SECTION Caveats
// Note that in some cases the widget can be picked even when it is "behind"
// other actors.  This is an intended feature and not a bug.
// 
// This class, and the affiliated vtkCustomBoxRepresentation, are second generation
// VTK widgets. An earlier version of this functionality was defined in the
// class vtkBoxWidget.

// .SECTION See Also
// vtkCustomBoxRepresentation vtkBoxWidget

#ifndef __vtkCustomBoxWidget_h
#define __vtkCustomBoxWidget_h

#include "vtkBoxWidget2.h"

class vtkCustomBoxRepresentation;


class VTK_EXPORT vtkCustomBoxWidget : public vtkBoxWidget2
{
public:
  // Description:
  // Instantiate the object.
  static vtkCustomBoxWidget *New();

  // Description:
  // Standard class methods for type information and printing.
  vtkTypeMacro(vtkCustomBoxWidget,vtkBoxWidget2);

  // Description:
  // Specify an instance of vtkWidgetRepresentation used to represent this
  // widget in the scene. Note that the representation is a subclass of vtkProp
  // so it can be added to the renderer independent of the widget.
  void SetRepresentation(vtkCustomBoxRepresentation *r)
    {this->Superclass::SetWidgetRepresentation(reinterpret_cast<vtkWidgetRepresentation*>(r));}
  
  // Description:
  // Create the default widget representation if one is not set. By default,
  // this is an instance of the vtkCustomBoxRepresentation class.
  void CreateDefaultRepresentation();

protected:
   vtkCustomBoxWidget();
  ~vtkCustomBoxWidget();

private:
  vtkCustomBoxWidget(const vtkCustomBoxWidget&);  //Not implemented
  void operator=(const vtkCustomBoxWidget&);  //Not implemented
};
 
#endif
