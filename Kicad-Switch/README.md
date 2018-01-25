Battery Management System Switch
--------------------------------

Version 2 of the PCB for the MOSFET switch matrix using SMD components.

Digital circuitry ensures that no two batteries are connected together, which
could cause damage if the state of charge between the batteries differs
significantly.

A diode matrix is used to provide power for the electronics. It ensures that
power is taken from the loads, or from the batteries if the loads are not
connected. This enables the firmware to keep track of the current drawn by
the local electronics.

The MOSFETs are driven by BJT PNP-NPN totem pole drivers. Three [Ideal Diodes](http://www.jiggerjuice.info/electronics/projects/power/ideal-diode.html) 
are also provided in the load circuits to prevent current flowing back to a
battery when a stronger battery is connected to a load. These circuits ensure
that additional voltage drops are kept small. The panel circuit will also
experience such a flow-back in circumstances when the panel voltage is
low. However prevention of this can be done in firmware as there is no value
connecting the panel to a battery if the panel voltage is low.

(c) K. Sarkies 19/01/2018

