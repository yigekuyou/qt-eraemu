#include "era_binary_data.h"
#include <QtEndian>
#include <limits>
#include <stdexcept>
#include <zlib.h>

namespace EraBinaryData {
namespace {
constexpr quint64 Magic = Q_UINT64_C(0x0a1a0a0d41524589);
constexpr quint64 ZipMagic = Q_UINT64_C(0x0a50495a41524589);
constexpr qsizetype MaxPayload = 256 * 1024 * 1024;
constexpr quint8 End = 0xff, Row = 0xe0, Plane = 0xe1;
constexpr quint8 Zero = 0xf0, ZeroRow = 0xf1, ZeroPlane = 0xf2;
// Bounds cover ordinary games while preventing hostile lengths/zero runs from allocating unbounded memory.
constexpr qsizetype MaxCells = 16 * 1024 * 1024;
[[noreturn]] void fail(const char* message) { throw std::runtime_error(message); }
qsizetype count(const Variable& v) {
    if (v.dimensions.size() > 3) fail("Invalid rank");
    qsizetype n = 1;
    for (int d : v.dimensions) {
        if (d < 0 || d > MaxCells || (d && n > MaxCells / d)) fail("Invalid array dimensions");
        n *= d;
    }
    return n;
}
QByteArray gzipPayload(const QByteArray& input, bool compress) {
    z_stream stream{};
    const int init = compress ? deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY)
                              : inflateInit2(&stream, 15 + 16);
    if (init != Z_OK) fail("Cannot initialize gzip");
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.constData()));
    stream.avail_in = input.size();
    QByteArray result;
    int status;
    do {
        char chunk[32768]; stream.next_out = reinterpret_cast<Bytef*>(chunk); stream.avail_out = sizeof(chunk);
        status = compress ? deflate(&stream, Z_FINISH) : inflate(&stream, Z_NO_FLUSH);
        const qsizetype produced = sizeof(chunk) - stream.avail_out;
        if ((status != Z_OK && status != Z_STREAM_END) || produced > MaxPayload - result.size()) {
            if (compress) deflateEnd(&stream); else inflateEnd(&stream);
            fail("Invalid or oversized gzip payload");
        }
        result.append(chunk, produced);
    } while (status != Z_STREAM_END);
    const bool trailing = stream.avail_in != 0;
    if (compress) deflateEnd(&stream); else inflateEnd(&stream);
    if (trailing) fail("Trailing gzip bytes");
    return result;
}
struct Writer {
    QByteArray data;
    void byte(quint8 n) { data.append(char(n)); }
    template<typename T> void fixed(T n) {
        char bytes[sizeof(T)]; qToLittleEndian<T>(n, bytes); data.append(bytes, sizeof(T));
    }
    void integer(qint64 n) {
        if (n >= 0 && n <= 0xcf) byte(quint8(n));
        else if (n >= -32768 && n <= 32767) { byte(0xd0); fixed<qint16>(qint16(n)); }
        else if (n >= std::numeric_limits<qint32>::min() && n <= std::numeric_limits<qint32>::max()) {
            byte(0xd1); fixed<qint32>(qint32(n));
        } else { byte(0xd2); fixed<qint64>(n); }
    }
    void string(const QString& s) {
        if (s.size() > std::numeric_limits<int>::max() / 2) fail("String too large");
        quint32 n = quint32(s.size() * 2);
        while (n >= 128) { byte(quint8(n | 128)); n >>= 7; }
        byte(quint8(n));
        for (QChar c : s) fixed<quint16>(c.unicode());
    }
    void variable(const Variable& v) {
        const qsizetype n = count(v);
        if (v.name.isEmpty() || (v.string ? v.strings.size() : v.integers.size()) != n)
            fail("Invalid variable values");
        byte(quint8((v.string ? 0x10 : 0) + v.dimensions.size())); string(v.name);
        if (v.dimensions.isEmpty()) {
            if (v.string) string(v.strings.first()); else integer(v.integers.first());
            return;
        }
        for (int d : v.dimensions) fixed<qint32>(d);
        // Explicit rows are valid 1808 streams; compress zero cells and omit trailing zeros.
        // No dependence on the in-memory representation or QDataStream's string encoding.
        const int width = v.dimensions.last();
        const qsizetype rows = width ? n / width : 0;
        for (qsizetype r = 0; r < rows; ++r) {
            qsizetype zeros = 0;
            for (int col = 0; col < width; ++col) {
                const qsizetype i = r * width + col;
                const bool empty = v.string ? v.strings.at(i).isEmpty() : v.integers.at(i) == 0;
                if (empty) { ++zeros; continue; }
                if (zeros) { byte(Zero); integer(zeros); zeros = 0; }
                if (v.string) { byte(0xd8); string(v.strings.at(i)); }
                else integer(v.integers.at(i));
            }
            if (v.dimensions.size() >= 2) byte(Row);
            if (v.dimensions.size() == 3 && (r + 1) % v.dimensions.at(1) == 0) byte(Plane);
        }
        byte(End);
    }
};
struct Reader {
    const QByteArray& data;
    qsizetype pos = 0, totalCells = 0;
    quint8 byte() { if (pos == data.size()) fail("Truncated stream"); return quint8(data.at(pos++)); }
    template<typename T> T fixed() {
        if (data.size() - pos < qsizetype(sizeof(T))) fail("Truncated stream");
        T n = qFromLittleEndian<T>(data.constData() + pos); pos += sizeof(T); return n;
    }
    qint64 integer(quint8 b) {
        if (b <= 0xcf) return b;
        if (b == 0xd0) return fixed<qint16>();
        if (b == 0xd1) return fixed<qint32>();
        if (b == 0xd2) return fixed<qint64>();
        fail("Invalid integer tag");
    }
    qint64 integer() { return integer(byte()); }
    QString string() {
        quint32 n = 0;
        for (int shift = 0; ; shift += 7) {
            quint8 b = byte();
            if (shift == 28 && b > 7) fail("Invalid string length");
            n |= quint32(b & 127) << shift;
            if (!(b & 128)) break;
            if (shift == 28) fail("Invalid string length");
        }
        if ((n & 1) || n > quint32(data.size() - pos)) fail("Invalid UTF-16 length");
        QString s; s.reserve(n / 2);
        for (quint32 i = 0; i < n / 2; ++i) s.append(QChar(fixed<quint16>()));
        return s;
    }
    Variable variable(quint8 tag) {
        if ((tag & 0xec) != 0 || (tag & 15) > 3) fail("Invalid variable type");
        Variable v; v.string = tag & 16; v.name = string();
        if (v.name.isEmpty()) fail("Empty variable name");
        const int rank = tag & 15;
        for (int i = 0; i < rank; ++i) v.dimensions.append(fixed<qint32>());
        qsizetype n = count(v);
        if (n > MaxCells - totalCells) fail("Too many cells");
        totalCells += n;
        if (v.string) v.strings.resize(n); else v.integers.resize(n);
        if (!rank) {
            if (v.string) v.strings[0] = string(); else v.integers[0] = integer();
            return v;
        }
        const qsizetype width = v.dimensions.last();
        const qsizetype planeSize = rank == 3 ? width * v.dimensions.at(1) : 0;
        qsizetype planeBase = 0, rowBase = 0, col = 0;
        for (;;) {
            quint8 b = byte();
            if (b == End) break;
            if (b == Row) {
                if (rank < 2 || rowBase >= n) fail("Unexpected row end");
                rowBase += width; col = 0; continue;
            }
            if (b == Plane) {
                if (rank != 3 || planeBase >= n) fail("Unexpected plane end");
                planeBase += planeSize; rowBase = planeBase; col = 0; continue;
            }
            if (b == ZeroRow || b == ZeroPlane) {
                const qint64 run = integer();
                const qsizetype stride = b == ZeroRow ? width : planeSize;
                if (col || rank < (b == ZeroRow ? 2 : 3) || run <= 0 || !stride || run > (n - rowBase) / stride)
                    fail("Invalid empty array run");
                rowBase += run * stride;
                if (b == ZeroPlane) planeBase = rowBase;
                continue;
            }
            if (b == Zero) {
                qint64 run = integer();
                if (run <= 0 || run > width - col || rowBase + col + run > n) fail("Invalid zero run");
                col += run; continue;
            }
            if (col >= width || rowBase + col >= n) fail("Array overflow");
            if (v.string) { if (b != 0xd8) fail("Invalid string tag"); v.strings[rowBase + col] = string(); }
            else v.integers[rowBase + col] = integer(b);
            ++col;
        }
        return v;
    }
};
}
bool encode(const Document& d, QByteArray& bytes, QString* error, bool gzip) {
    try {
        if (quint8(d.type) > 3 || ((d.type == FileType::Global || d.type == FileType::Var) && !d.characters.isEmpty())
            || (d.type == FileType::Character && !d.variables.isEmpty())) fail("Invalid document shape");
        Writer w; w.fixed<quint64>(Magic); w.fixed<quint32>(1808); w.fixed<quint32>(0);
        w.byte(quint8(d.type)); w.fixed<qint64>(d.uniqueCode); w.fixed<qint64>(d.scriptVersion); w.string(d.description);
        if (d.type == FileType::Normal || d.type == FileType::Character) {
            w.fixed<qint64>(d.characters.size());
            for (const Character& c : d.characters) {
                for (const Variable& v : c.builtins) w.variable(v);
                if (!c.userDefined.isEmpty()) { w.byte(0xfd); for (const Variable& v : c.userDefined) w.variable(v); }
                w.byte(0xfe);
            }
        }
        for (const Variable& v : d.variables) w.variable(v);
        w.byte(End);
        if(d.type==FileType::Normal || d.type==FileType::Global) w.byte(End);
        if (gzip) {
            QByteArray header = w.data.left(16);
            qToLittleEndian<quint64>(ZipMagic, header.data());
            bytes = header + gzipPayload(w.data.mid(16), true);
        } else bytes = std::move(w.data);
        return true;
    } catch (const std::exception& e) { if (error) *error = QString::fromUtf8(e.what()); return false; }
}
bool decode(const QByteArray& bytes, Document& document, QString* error) {
    try {
        QByteArray decodedBytes = bytes;
        Reader r{decodedBytes};
        const quint64 magic = r.fixed<quint64>();
        if ((magic != Magic && magic != ZipMagic) || r.fixed<quint32>() != 1808) fail("Unsupported save format");
        quint32 extensions = r.fixed<quint32>();
        if (extensions > quint32((bytes.size() - r.pos) / 4)) fail("Truncated header");
        for (quint32 i = 0; i < extensions; ++i) r.fixed<quint32>();
        if (magic == ZipMagic) decodedBytes = decodedBytes.left(r.pos) + gzipPayload(decodedBytes.mid(r.pos), false);
        Document d; quint8 type = r.byte(); if (type > 3) fail("Invalid file type");
        d.type = FileType(type); d.uniqueCode = r.fixed<qint64>(); d.scriptVersion = r.fixed<qint64>(); d.description = r.string();
        if (d.type == FileType::Normal || d.type == FileType::Character) {
            qint64 n = r.fixed<qint64>();
            if (n < 0 || n > decodedBytes.size() - r.pos) fail("Invalid character count");
            for (qint64 i = 0; i < n; ++i) {
                Character c; bool user = false;
                for (;;) {
                    quint8 tag = r.byte();
                    if (tag == 0xfe) break;
                    if (tag == 0xfd) { if (user) fail("Repeated character separator"); user = true; continue; }
                    (user ? c.userDefined : c.builtins).append(r.variable(tag));
                }
                d.characters.append(std::move(c));
            }
        }
        for (;;) {
            quint8 tag = r.byte(); if (tag == End) break;
            if (d.type == FileType::Character) fail("Variables after character list");
            d.variables.append(r.variable(tag));
        }
        // EM writes a second EOF for its optional extension segment.
        // Map/XML/DT payloads are not modelled yet; reject them atomically.
        if((d.type==FileType::Normal || d.type==FileType::Global) && r.pos<decodedBytes.size()) {
            if(r.byte()!=End) fail("Unsupported EM extension variables");
        }
        if (r.pos != decodedBytes.size()) fail("Trailing save bytes");
        document = std::move(d); return true;
    } catch (const std::exception& e) { if (error) *error = QString::fromUtf8(e.what()); return false; }
}
namespace {
const QString Fin = QStringLiteral("__FINISHED"), Sep = QStringLiteral("__EMU_SEPARATOR__"), Start = QStringLiteral("__EMUERA_1808_STRAT__");
QStringList names(const char* s) { return QString::fromLatin1(s).split(' ',Qt::SkipEmptyParts); }
QStringList baseInts(bool chara) {
    if (chara) return names("BASE MAXBASE ABL TALENT EXP MARK PALAM SOURCE EX CFLAG JUEL RELATION EQUIP TEQUIP STAIN GOTJUEL NOWEX");
    auto n=names("DAY MONEY ITEM FLAG TFLAG UP PALAMLV EXPLV EJAC DOWN RESULT COUNT TARGET ASSI MASTER NOITEM LOSEBASE SELECTCOM ASSIPLAY PREVCOM NOTUSE_14 NOTUSE_15 TIME ITEMSALES PLAYER NEXTCOM PBAND BOUGHT NOTUSE_1C NOTUSE_1D");
    for(char c='A';c<='Z';++c) n.append(QString(QChar(c)));
    n.append(names("NOTUSE_38 NOTUSE_39 NOTUSE_3A NOTUSE_3B")); return n;
}
QStringList builtinNames(bool chara) {
    auto n=baseInts(chara);
    n.append(chara ? names("NAME CALLNAME NICKNAME MASTERNAME ISASSI NO CSTR CDFLAG DOWNBASE CUP CDOWN TCVAR")
                   : names("SAVESTR TSTR RANDDATA DITEMTYPE DA DB DC DD DE TA TB")); return n;
}
Variable lookup(const QList<Variable>& vars,const QString& name,bool str,int rank) {
    for(const auto& v:vars) if(v.name==name) return v;
    Variable v; v.name=name; v.string=str; for(int i=0;i<rank;++i)v.dimensions.append(0);
    if(!rank) { if(str)v.strings.append(QString());else v.integers.append(0); } return v;
}
QString cell(const Variable& v,int i) { return v.string ? v.strings.value(i) : QString::number(v.integers.value(i)); }
void line(QStringList& lines,const QString& value) {
    if(value.contains('\r') || value.contains('\n') || value==Fin || value==Sep || value==Start) fail("String cannot be represented in native text save");
    lines.append(value);
}
void arrayLines(QStringList& lines,const Variable& v) {
    if(v.string && v.dimensions.size()>1) fail("C# text does not support multidimensional strings");
    int last=int(count(v))-1;
    while(last>=0 && (v.string ? v.strings.value(last).isEmpty() : v.integers.value(last)==0)) --last;
    if(v.dimensions.size()==1) for(int i=0;i<=last;++i) line(lines,cell(v,i));
    else {
        const int width=v.dimensions.last(), height=v.dimensions.size()==3 ? v.dimensions[1] : 1;
        const int planes=v.dimensions.size()==3 && width && height ? last/(width*height)+1 : (last>=0 ? 1:0);
        for(int p=0;p<planes;++p) {
            if(v.dimensions.size()==3) lines.append(QString::number(p)+"{");
            const int rows=v.dimensions.size()==3 ? height : v.dimensions[0];
            int lastRow=-1;
            for(int r=0;r<rows;++r) for(int c=0;c<width;++c) if(v.integers.value((p*rows+r)*width+c)!=0) lastRow=r;
            for(int r=0;r<=lastRow;++r) {
                int lastCol=-1; for(int c=0;c<width;++c) if(v.integers.value((p*rows+r)*width+c)!=0) lastCol=c;
                QStringList cells; for(int c=0;c<=lastCol;++c)cells.append(cell(v,(p*rows+r)*width+c));
                lines.append(cells.join(','));
            }
            if(v.dimensions.size()==3) lines.append("}");
        }
    }
    lines.append(Fin);
}
void baseWrite(QStringList& lines,const QList<Variable>& vars,bool chara) {
    if(chara) {
        for(const auto& n:names("NAME CALLNAME")) line(lines,cell(lookup(vars,n,true,0),0));
        for(const auto& n:names("ISASSI NO")) line(lines,cell(lookup(vars,n,false,0),0));
    }
    for(const auto& n:baseInts(chara)) arrayLines(lines,lookup(vars,n,false,1));
    if(!chara) arrayLines(lines,lookup(vars,"SAVESTR",true,1));
}
void sectionsWrite(QStringList& lines,const QList<Variable>& vars,bool chara,bool userOnly=false) {
    const auto known=builtinNames(chara); const auto base=baseInts(chara);
    const int sections=userOnly ? 6 : (chara ? 6 : 14);
    for(int section=0;section<sections;++section) {
        const bool user=userOnly || section>=8;
        const int slot=userOnly ? section+2 : section>=8 ? section-6 : section;
        const bool str=slot%2==0; const int rank=slot/2;
        for(const auto& v:vars) {
            const bool isBuiltin=known.contains(v.name);
            if(v.string!=str || v.dimensions.size()!=rank || (user ? isBuiltin : !isBuiltin)) continue;
            if(!user && (base.contains(v.name) || v.name=="SAVESTR" || (chara && names("NAME CALLNAME ISASSI NO").contains(v.name)))) continue;
            if(rank==0) { if(str ? !v.strings.value(0).isEmpty() : v.integers.value(0)!=0) line(lines,v.name+":"+cell(v,0)); }
            else {
                bool any=false; if(str) { for(const auto& value:v.strings) any|=!value.isEmpty(); }
                else { for(auto value:v.integers) any|=value!=0; }
                if(!any)continue;
                line(lines,v.name); arrayLines(lines,v);
            }
        }
        lines.append(Sep);
    }
}
struct TextReader {
    QStringList lines; int pos=0; qsizetype total=0;
    QString take() { if(pos>=lines.size())fail("Truncated text save");return lines[pos++]; }
    qint64 number(const QString& text) { bool ok=false; auto n=text.toLongLong(&ok);if(!ok)fail("Invalid text integer");return n; }
    Variable array(QString name,bool str,int rank) {
        Variable v;v.name=name;v.string=str;
        QList<QList<QStringList>> planes; QList<QStringList> rows;
        for(;;) {
            const QString text=take(); if(text==Fin)break;
            if(text==Sep)fail("Unexpected text section boundary");
            if(rank==3) {
                if(text!=QString::number(planes.size())+"{")fail("Invalid text plane");
                QList<QStringList> plane;
                for(;;) { const auto row=take(); if(row=="}")break; if(row==Fin || row==Sep)fail("Unterminated text plane");plane.append(row.isEmpty()?QStringList{}:row.split(',')); }
                planes.append(plane);
            } else rows.append(rank==1 ? QStringList{text} : text.isEmpty()?QStringList{}:text.split(','));
            if(++total>MaxCells)fail("Too many text cells");
        }
        if(rank!=3)planes.append(rows);
        int h=0,w=0;for(const auto& plane:planes) { h=qMax(h,int(plane.size()));for(const auto& row:plane)w=qMax(w,int(row.size())); }
        v.dimensions=rank==1 ? QList<int>{h} : rank==2 ? QList<int>{h,w} : QList<int>{int(planes.size()),h,w};
        const int n=count(v); if(n>MaxCells-total)fail("Too many text cells");total+=n;
        if(str)v.strings.resize(n);else v.integers.resize(n);
        for(int p=0;p<planes.size();++p)for(int r=0;r<planes[p].size();++r)for(int c=0;c<planes[p][r].size();++c) {
            const int i=rank==1 ? r : (p*h+r)*w+c;
            if(str)v.strings[i]=planes[p][r][c];else v.integers[i]=number(planes[p][r][c]);
        } return v;
    }
    void baseRead(QList<Variable>& vars,bool chara) {
        if(chara) {
            for(const auto& n:names("NAME CALLNAME")) { Variable v;v.name=n;v.string=true;v.strings={take()};vars.append(v); }
            for(const auto& n:names("ISASSI NO")) { Variable v;v.name=n;v.integers={number(take())};vars.append(v); }
        }
        for(const auto& n:baseInts(chara))vars.append(array(n,false,1));
        if(!chara)vars.append(array("SAVESTR",true,1));
    }
    void sectionsRead(QList<Variable>& vars,bool chara,bool userOnly=false) {
        const int sections=userOnly ? 6 : (chara ? 6 : 14);
        for(int section=0;section<sections;++section) {
            const int slot=userOnly ? section+2 : section>=8 ? section-6 : section;
            const bool str=slot%2==0;const int rank=slot/2;
            for(;;) {
                auto key=take();if(key==Sep)break;
                if(key.isEmpty() || key==Fin)fail("Invalid text variable name");
                if(rank)vars.append(array(key,str,rank));
                else { int colon=key.indexOf(':');if(colon<1)fail("Invalid text scalar"); Variable v;v.name=key.left(colon);v.string=str;
                    if(str)v.strings={key.mid(colon+1)};else v.integers={number(key.mid(colon+1))};vars.append(v); }
            }
        }
    }
};
}
bool encodeText(const Document& d,QByteArray& bytes,QString* error) {
    try {
        if(d.type!=FileType::Normal && d.type!=FileType::Global)fail("Native text supports normal and GLOBAL only");
        QStringList lines{QString::number(d.uniqueCode),QString::number(d.scriptVersion)};
        if(d.type==FileType::Global) {
            arrayLines(lines,lookup(d.variables,"GLOBAL",false,1));arrayLines(lines,lookup(d.variables,"GLOBALS",true,1));
            lines.append(Start);
            auto user=d.variables;for(auto it=user.begin();it!=user.end();)if(it->name=="GLOBAL" || it->name=="GLOBALS")it=user.erase(it);else ++it;
            sectionsWrite(lines,user,false,true);
        } else {
            line(lines,d.description);lines.append(QString::number(d.characters.size()));
            for(const auto& c:d.characters) { if(!c.userDefined.isEmpty())fail("Reference C# native text does not save user character variables");baseWrite(lines,c.builtins,true); }
            baseWrite(lines,d.variables,false);lines.append(Start);
            for(const auto& c:d.characters)sectionsWrite(lines,c.builtins,true);
            sectionsWrite(lines,d.variables,false);
        }
        bytes=QByteArray::fromHex("efbbbf")+(lines.join("\r\n")+"\r\n").toUtf8();return true;
    }catch(const std::exception& e){if(error)*error=QString::fromUtf8(e.what());return false;}
}
bool decodeText(const QByteArray& bytes,const Document& schema,Document& document,QString* error) {
    try {
        if(schema.type!=FileType::Normal && schema.type!=FileType::Global)fail("Unsupported text container");
        QString text=QString::fromUtf8(bytes);if(text.startsWith(QChar(0xfeff)))text.remove(0,1);
        text.replace("\r\n","\n");if(text.contains('\r'))fail("Invalid text line endings");
        TextReader r{text.split('\n')};if(r.lines.last().isEmpty())r.lines.removeLast();
        Document d;d.type=schema.type;d.uniqueCode=r.number(r.take());d.scriptVersion=r.number(r.take());
        if(d.type==FileType::Global) {
            d.variables.append(r.array("GLOBAL",false,1));d.variables.append(r.array("GLOBALS",true,1));
        } else {
            d.description=r.take();const auto n=r.number(r.take());if(n<0 || n>r.lines.size())fail("Invalid text character count");
            for(qint64 i=0;i<n;++i) { Character c;r.baseRead(c.builtins,true);d.characters.append(c); }
            r.baseRead(d.variables,false);
        }
        if(r.take()!=Start)fail("Unsupported text save version");
        if(d.type==FileType::Global)r.sectionsRead(d.variables,false,true);
        else { for(auto& c:d.characters)r.sectionsRead(c.builtins,true);r.sectionsRead(d.variables,false); }
        if(r.pos!=r.lines.size())fail("Trailing text save lines");
        document=std::move(d);return true;
    }catch(const std::exception& e){if(error)*error=QString::fromUtf8(e.what());return false;}
}

}
