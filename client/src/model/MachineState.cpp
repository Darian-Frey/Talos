#include "MachineState.h"

MachineState MachineState::fromRegsReply(const QList<QByteArray> &tokens)
{
    MachineState state;
    if (tokens.isEmpty() || tokens.first() != "OK")
        return state;

    // The regs reply brackets each pair with separators: RemoteDebug_regs emits
    // "OK" then send_sep(), and each send_key_value() itself starts with
    // send_sep() — so splitting on 0x1 leaves empty tokens between the pairs.
    // Drop them; the remainder after "OK" is strictly alternating name, hexvalue.
    QList<QByteArray> flat;
    flat.reserve(tokens.size());
    for (int i = 1; i < tokens.size(); ++i) {
        if (!tokens[i].isEmpty())
            flat.append(tokens[i]);
    }

    for (int i = 0; i + 1 < flat.size(); i += 2) {
        const QString name = QString::fromLatin1(flat[i]);
        bool ok = false;
        const quint32 val = flat[i + 1].toUInt(&ok, 16);
        if (!name.isEmpty() && ok)
            state.values.insert(name, val);
    }
    return state;
}
