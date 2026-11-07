'use strict';
// Read loc/yaw at a given actor pointer (no object scan).
rpc.exports = {
  read: function (addr) {
    const o = ptr(addr);
    const root = o.add(0x130).readPointer();
    const x = root.add(0x1D0).readFloat(), y = root.add(0x1D4).readFloat(), z = root.add(0x1D8).readFloat();
    const qx = root.add(0x1C0).readFloat(), qy = root.add(0x1C4).readFloat(), qz = root.add(0x1C8).readFloat(), qw = root.add(0x1CC).readFloat();
    const yaw = Math.atan2(2 * (qw * qz + qx * qy), 1 - 2 * (qy * qy + qz * qz)) * 180 / Math.PI;
    return { x: x, y: y, z: z, yaw: yaw };
  }
};
