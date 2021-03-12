const wlmio = require("./build/Release/wlmio");

module.exports = wlmio;


function packRegisterAccess(type, value)
{
  const buffer = new ArrayBuffer(260);
  const typeView = new Uint8Array(buffer, 0, 1);
  const lengthView = new Uint16Array(buffer, 2, 1);
  let valueView;

  typeView[0] = type;
  
  switch(type)
  {
    case 0:
    default:
      typeView[0] = 0;
      break;

    case 9:
      lengthView[0] = Math.min(value.length, 64);
      valueView = new Uint32Array(buffer, 4, 64);
      break;

    case 10:
      lengthView[0] = Math.min(value.length, 128);
      valueView = new Uint16Array(buffer, 4, 128);
      break;

    case 11:
      lengthView[0] = Math.min(value.length, 256);
      valueView = new Uint8Array(buffer, 4, 256);
      break;
  }

  if(valueView)
  { valueView.set(value); }

  return buffer;
}
module.exports.packRegisterAccess = packRegisterAccess;


function unpackRegisterAccess(buffer)
{
  const typeView = new Uint8Array(buffer, 0, 1);
  const lengthView = new Uint16Array(buffer, 2, 1);
  let valueView;

  let type = typeView[0];
  const length = lengthView[0];

  switch(type)
  {
    case 0:
    default:
      type = 0;
      break;

    case 9:
      valueView = new Uint32Array(buffer, 4, length);
      break;

    case 10:
      valueView = new Uint16Array(buffer, 4, length);
      break;

    case 11:
      valueView = new Uint8Array(buffer, 4, length);
      break;
  }

  let value = null;
  if(valueView)
  { value = Array.from(valueView); }

  return { type: type, value: value };
}
module.exports.unpackRegisterAccess = unpackRegisterAccess;


function unpackNodeInfo(buffer)
{
  const versionView = new Uint8Array(buffer, 0, 6);
  const vcsIdView = new BigUint64Array(buffer, 8, 1);
  const uidView = new Uint8Array(buffer, 16, 16);
  const nameView = new Uint8Array(buffer, 32, 51);
  const crcView = new BigUint64Array(buffer, 88, 1);

  const nameLen = nameView.indexOf(0);
  const nameBuffer = Buffer.from(buffer, 32, nameLen);

  return {
    protocolVersion: { major: versionView[0], minor: versionView[1] },
    hardwareVersion: { major: versionView[2], minor: versionView[3] },
    softwareVersion: { major: versionView[4], minor: versionView[5] },
    vcsId: vcsIdView[0],
    uid: uidView,
    name: nameBuffer.toString("utf8"), 
    crc: crcView[0]
  };
}
module.exports.unpackNodeInfo = unpackNodeInfo;

