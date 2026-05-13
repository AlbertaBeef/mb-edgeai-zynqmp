# meta-ros2-jazzy's xtl_0.7.7 pins ;branch=master but the SRCREV a7c1c5...
# is no longer reachable from master (xtensor-stack force-pushed master).
# Drop the branch constraint so bitbake finds the commit via any ref.
SRC_URI = "git://github.com/xtensor-stack/xtl.git;protocol=https;nobranch=1"
