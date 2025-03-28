# Virtual shifting 
This section describes how the virtual shifting inside SHIFTR works as it isn't natively supported by the trainers (e.g. TACX Neo 2T).

***PARTLY OUTDATED***: As there have been many changes in the recent version this description isn't completely correct anymore and needs to be updated.

## How it works
The virtual gears are defined inside Zwift and don't need a special handling in SHIFTR. Zwift just sends the corresponding gear ratio (chainring:sprocket) between 0.75 and 5.49 on every shift. 

Currently these are: 0.75 0.87 0.99 1.11 1.23 1.38 1.53 1.68 1.86 2.04 2.22 2.40 2.61 2.82 3.03 3.24 3.49 3.74 3.99 4.24 4.54 4.84 5.14 5.49

As the Zwift Cog has 14 teeth and a standard chainring 34 teeth the default ratio in SHIFTR is defined at 2.4286 which roughly matches Zwift Gear 12. This ratio is the base for all further gear calculations. Of course the chainring and sprocket teeth can be set in the device configuration and these values will then be used for the default ratio.

Based on this ratio and the selected ratio from Zwift (e.g. 1.23 ~ "Gear 5") the ratio that will later be applied to the trainer's force will be calculated:

$R_{relative} = R_{selected} / R_{standard}$ 

Example: 

$R_{relative} = 1.23 / 2.4286 = 0.51$ (rounded)

To set the correct resistance of the trainer, the gravitational, the rolling and the drag force are calculated using formulas. There are currently three methods of applying the virtual gears implemented: "***Basic Resistance***", "***Target Power***" and "***Track Resistance***". The feeling may vary from trainer to trainer but whatever mode is being used the reported watts and cadence are never modified and so the measured power in Zwift will always be as correct as before.

The different modes can be set in the device configuration - the default is "Basic Resistance".

Getting back to the calculations: The combined weight of you (the cyclist) and your bike is $W$ ($kg$). The gravitational force constant $g$ is 9.8067 ($m/s^2$). There is a dimensionless parameter, called the "Coefficient of Rolling Resistance", or $C_{rr}$, that captures the bumpiness of the road and the quality of your tires. There are some defaults specified in the FE-C docs:

| Terrain | Coefficient of Rolling Resistance | 
  |-|-:|
  | Wooden Track | 0.001 |
  | Smooth Concrete | 0.002 |
  | Asphalt Road | 0.004 |
  | Rough Road | 0.008 |

This parameter is set by Zwift in normal SIM mode as `0x53`(= 83 dec) and equals (as the unit is $5·10^{-5}$) a value of 0.00415 which is kind of an "Asphalt Road" and also the default mentioned in the FE-C docs. The steepness of a hill will be provided by Zwift in terms of percentage grade $G$.

Regarding the aerodynamic drag the head/tailwind $V_{hw} (m/s)$ isn't taken into account from Zwift and is assumed 0. But the faster your groundspeed $V_{gs} (m/s)$ is, the more force the air pushes against you. Your airspeed $V_{as} (m/s)$ is the speed that the wind strikes your face, and it is the sum of your groundspeed $V_{gs}$ and the headwind speed $V_{hw}$. As well, you and your bike present a certain frontal area $A (m^2)$ to the air. The larger this frontal area, the more air you have to displace, and the larger the force the air pushes against you. The air density $Rho (kg/m^3)$ is also important. The more dense the air, the more force it exerts on you. Then there is another dimensionless parameter, called the "Drag Coefficient", or $C_{d}$, that captures other effects, like the slipperiness of your clothing and the degree to which air flows laminarly rather than turbulently around you and your bike. Having all the values we can calculate as follows:

### Calculate total geared force

$F_{gravity} = g · \sin(\arctan(\frac{G}{100})) · W$

$F_{rolling} = g · W · C_{rr}$

As we are calculating with no head/tailwind, our $V_{as}$ equals our $V_{gs}$ as we assume $V_{hw} = 0$ and the calculation would be:

$V_{as} = V_{gs} + V_{hw}$

$F_{drag} = 0.5 · C_d · A · Rho · V_{as}^2$

The FE-C documentation uses a more simple approach by using a bundled coefficient $C_{wr} (kg/m)$ as the "Wind Resistance Coefficient" there:

$C_{wr} = A · C_d · Rho$

| Bicycle and Rider | Frontal Area ($m^2$) | Drag Coefficient |  
  |-|-:|-:|
  | All-terrain (Mountain) Bike | 0.57 | 1.20 |
  | Upright Commuting Bike | 0.55 | 1.15 |
  | Road Bike, Touring Position | 0.40 | 1.0 |
  | Racing Bike, Rider Crouched, Tight Clothing | 0.36 | 0.88 |

As a unit this coefficient uses 0.01 kg/m and the default value of the trainer is `0x33` (=51 dec) which equals 0.51 kg/m while using the standard density of air, 1.275kg/m3 (15°C at sea level) and the "Road Bike" parameters for frontal area and drag coefficient as the default.

$F_{drag} = 0.5 · C_{wr} · V_{as}^2$

The total force is the sum of these three forces:

$F_{total} = F_{gravity} + F_{rolling} + F_{drag}$

In case of $F_{total} >= 0$ the relative gear ratio mentioned above will be applied by multiplication:

$F_{totalGeared} = F_{total} · R_{relative}$

Otherwise in case of $F_{total} < 0$ the reciprocal of the relative gear ratio will be applied by multiplication:

$F_{totalGeared} = F_{total} · \frac{1}{R_{relative}}$

In the "Basic resistance" mode the resulting force will then be used to set the trainer's resistance. Depending on the model there is a maximum force the trainer can apply. For a Tacx Vortex this is 50N and for a Tacx Neo 2T this is 200N. On every connection the maximum force is read out of the trainer. The basic resistance can only be set in 0.5% (0-200) and not in N as expected. So before applying the resistance it will be mapped to the correct 0.5% value. 

In the "Track Resistance" mode we can only set a grade/slope on the trainer. The resulting "geared" force is being used to calculate backwards to get a correct percentage of the desired grade/slope with the gear ratio calculated in. This is done by first calculating the force as above but then subtracting the rolling and drag force to only have the gravity force with its gearing factor left. From there we are calculating the grade, that would need to be set to achieve the force we calculated before:

$F_{gravity} = F_{totalGeared} - F_{rolling} - F_{drag}$

$G = \tan(\arcsin(F_{gravity} / W / g)) · 100$

This new grade value is then sent to the trainer in the track resistance mode and so will also support the powered decline feature to provide a better feeling especially when riding downhill.

The "Target Power" mode uses a different approach. Here we want to set an expected watts value depending on the current track and speed. To calculate the speed we assume a default wheel diameter $d$ of 0.7m and take the current cadence $c$ from the trainer that is being multiplied with the wanted gear ratio. The product is then being multiplied with the calculated perimeter of the wheel:

$V_{gs} = (c · R_{selected}) · (d · \pi)$

This speed is then taken to calculate the geared total force [as described above](#calculate-total-geared-force) which is then being used to calculate the needed power $P$:

$P = F_{totalGeared} · V_{gs}$

This value is then being sent to the trainer in the target power mode as in ERG mode but of course is updated on every parameter change.
