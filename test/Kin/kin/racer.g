#{ shape:marker size=[.5 0 0 0] }

world { }
transX {}
transY {}
transPhi {}

base { shape:box size=[.15 .02 .6 0] color=[.1 .1 .1] }

(world transX) { joint:transX }
(transX transY) { joint:transY }
(transY transPhi) { joint:hingeZ }
(transPhi base) { joint:hingeX A=<T t(0 0 .05) > B=<T t(0 0 .3) >  Q=<T d(10 1 0 0)> }

wheelL{ shape:cylinder size=[0 0 .02 .05] }
wheelR{ shape:cylinder size=[0 0 .02 .05] }
(base wheelL) { joint:hingeX from=<T t(-.1 0 -.3)> to=<T d(90 0 1 0)> }
(base wheelR) { joint:hingeX from=<T t(.1 0 -.3)> to=<T d(90 0 1 0)> }

