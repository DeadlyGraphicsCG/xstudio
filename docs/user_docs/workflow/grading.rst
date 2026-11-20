.. _grading:

Grading Tool
============

.. figure:: ../images/grading-01.png
   :alt: xSTUDIO Grading tool in action
   :figclass: align-center
   :align: center
   :scale: 50 %

   The grading tool in action


xSTUDIO provides a grading tool allowing for the creation and manipulation of **SOP** grades that can be applied to a media item or a single frame or some frame range within the media item's ovwerall duration. Mask shapes can be created very easily to mask in or out an area of the image. In conjunction with xSTUDIO's notes system, this allows you to make creative grading notes with lots of control to record and share ideas. Multiple grades can also be 'stacked' on a single media item if so desired.

Creating a Grade
----------------

With the media frame of interest on the screen and the Grading tool open (this can either be as a floating window or within a panel in an xSTUDIO :ref:`interface layout <interface_layouts>`) you can either hit the **+** button to create a new grade layour or just start moving the colour wheel handles in the grading interface.

Editing a Grade
---------------

When you want to change an existing grade, click on the grade layer item in the list that is on the left hand side of the Grading panel.
* Delete the selected grade with the trashcan button
* Set the grade to be a single frame or the full duration of the media clip with the corresponding button

* Double-click a grade layer (or use **Rename...** in the layer menu) to give it a name. Press Enter to keep the name or Escape to cancel. The name is saved with the grade.

Adjusting Values
----------------

* **Exposure**, **Contrast** and **Saturation** each have **R**, **G** and **B** fields under their slider. These change one colour channel on top of the main value: the exposure fields are added to the main exposure (in stops), the contrast and saturation fields are multiplied with the main value. The reset button above a slider also resets its R, G and B fields.
* Every number field can be dragged left or right with the mouse to change its value. Hold **Shift** for small steps or **Ctrl** for big steps. Double-click a field (or a slider) to reset it to its default value. A single click lets you type a value.

Working Space
-------------

Each grade layer is applied in a **working space**, picked from the colour spaces of the OCIO config used by the media (for example ``scene_linear`` or ``compositing_log``, or a log space such as ACEScct). The image is converted into that space before the layer is applied and converted back afterwards. Choose it with the **Working space** menu above the sliders. This needs media whose colour management names an OCIO config (normally set by your pipeline).

Channel Check
-------------

Use the **R**, **G** and **B** buttons next to the working space menu to see a single colour channel in the viewer while grading. Click the button again to go back to all channels. These buttons use the same channel mode as the viewer toolbar and the viewer **R**, **G** and **B** hotkeys.

Polygon Grade Masks
-------------------

* Add a mask shape by hitting the 'Polygon' button
* When adding a polygon, click into the viewport to place verts for the polygon border. Clicking on the initial point will close the shape and make the polygon.
* The polygon can be edited by dragging its points. 
* Double Click on the polygon boundary to add a new point
* SHIFT+Click when the mouse is hopvered over a polygon point will delete it.

Ellipse Grade Masks
-------------------

* Add an oval mask shape with the 'Ellipse' button
* Handles on the viewport overlay

.. note::
    Use the softness control (drag left/right) to control the fall-off at the edge of ellipse and polygon masks. The mask effect can be *inverted* with the corresponding button.

Grading Walkthrough
-------------------

This video demostrates grading regions of a plate with polygons and an ellipse and adding a grade to the whole image:

.. raw:: html
    
   <center><video src="../../_static/grading-01.webm" width="720" height="366" controls></video></center>

