#include "MachineState.h"

MachineState MachineState::fromRegsReply(const QList<QByteArray> &tokens)
{
    MachineState state;
    if (tokens.isEmpty() || tokens.first() != "OK")
        return state;

    // After the leading "OK", tokens alternate name, hexvalue, name, hexvalue...
    for (int i = 1; i + 1 < tokens.size(); i += 2) {
        const QString name = QString::fromLatin1(tokens[i]);
        bool ok = false;
        const quint32 val = tokens[i + 1].toUInt(&ok, 16);
        if (!name.isEmpty() && ok)
            state.values.insert(name, val);
    }
    return state;
}
