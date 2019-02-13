/*
 * Version:  1.0
 * Author:   Juan Zhou
 * Date-Time:2017-08-22 14:46:30
 * Description: use the macro QAXFACTORY_DEFAULT to dexport the OCX
 *              first param  : the class-name which will be exported
 *              second param : CLSID
 *              third param  : InterfaceID
 *              fourth param : EventID
 *              fifth param  : TypeID
 *              sixth param  : ParagramID
 */
#include <QAxFactory>
#include "mediaplayer.h"
//MediaPlayer gg;
/*QAXFACTORY_DEFAULT(MediaPlayer,
                   "{e082f857-19b0-45f7-b940-41de3c0cf772}",
                   "{a623f1e7-6888-4c9c-b91e-160f39411ea2}",
                   "{fc7ab2b8-fd88-4197-b95c-b2b0b70fa70c}",
                   "{5b0b0534-ca38-4f51-9e83-5da45427f980}",
                   "{df2e15f8-a676-4e49-b0fe-c6fa0d6a5c90}")



*/

QAXFACTORY_BEGIN(
    "{5b0b0534-ca38-4f51-9e83-5da45427f980}", // type library ID
    "{df2e15f8-a676-4e49-b0fe-c6fa0d6a5c90}") // application ID
    QAXCLASS(MediaPlayer)
QAXFACTORY_END()
