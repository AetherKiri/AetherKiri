//---------------------------------------------------------------------------
// KiriKiri Cx protected XP3 archive decoder.
//
// The Cx virtual-machine algorithm is based on GARbro's MIT-licensed
// KiriKiriCx implementation:
// https://github.com/nanami5270/GARbro-Mod/blob/master/ArcFormats/KiriKiri/KiriKiriCx.cs
//---------------------------------------------------------------------------

#include "XP3ArchiveCxDecoder.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {

enum class Op : std::uint32_t {
    Nop,
    Ret,
    MovEdiArg,
    PushEbx,
    PopEbx,
    PushEcx,
    PopEcx,
    MovEaxEbx,
    MovEbxEax,
    MovEcxEbx,
    MovEaxEdi,
    MovEaxIndirect,
    AddEaxEbx,
    SubEaxEbx,
    MulEaxEbx,
    AndEcx0f,
    ShrEbx1,
    ShlEax1,
    ShrEaxCl,
    ShlEaxCl,
    OrEaxEbx,
    NotEax,
    NegEax,
    DecEax,
    IncEax,
    MovEaxImmediate,
    AndEbxImmediate,
    AndEaxImmediate,
    XorEaxImmediate,
    AddEaxImmediate,
    SubEaxImmediate,
};

struct Instruction {
    Op Code;
    std::uint32_t Immediate{};
};

struct CxScheme {
    std::uint32_t Fingerprint;
    std::uint32_t Mask;
    std::uint32_t Offset;
    std::array<std::uint8_t, 3> PrologOrder;
    std::array<std::uint8_t, 6> OddBranchOrder;
    std::array<std::uint8_t, 8> EvenBranchOrder;
    std::array<std::uint32_t, 1024> ControlBlock;
};

class CxProgram {
    static constexpr int LengthLimit = 0x80;

    std::uint32_t Seed;
    const std::array<std::uint32_t, 1024> &ControlBlock;
    int Length{};
    std::vector<Instruction> Code;

public:
    CxProgram(std::uint32_t seed,
              const std::array<std::uint32_t, 1024> &controlBlock) :
        Seed(seed), ControlBlock(controlBlock) {
        Code.reserve(LengthLimit);
    }

    void Clear() {
        Length = 0;
        Code.clear();
    }

    bool EmitNop(int count) {
        if(Length + count > LengthLimit)
            return false;
        Length += count;
        return true;
    }

    bool Emit(Op op, int length = 1) {
        if(Length + length > LengthLimit)
            return false;
        Length += length;
        Code.push_back({ op, 0 });
        return true;
    }

    bool EmitImmediateData(std::uint32_t immediate) {
        if(Length + 4 > LengthLimit)
            return false;
        Length += 4;
        Code.back().Immediate = immediate;
        return true;
    }

    bool EmitWithImmediate(Op op, std::uint32_t immediate,
                           int opLength = 1) {
        return Emit(op, opLength) && EmitImmediateData(immediate);
    }

    bool EmitWithRandom(Op op, int opLength = 1) {
        if(!Emit(op, opLength))
            return false;
        const std::uint32_t value = GetRandom();
        return EmitImmediateData(value);
    }

    std::uint32_t GetRandom() {
        const std::uint32_t oldSeed = Seed;
        Seed = 1103515245u * oldSeed + 12345u;
        return Seed ^ (oldSeed << 16u) ^ (oldSeed >> 16u);
    }

    std::uint32_t Execute(std::uint32_t hash) const {
        std::uint32_t eax = 0;
        std::uint32_t ebx = 0;
        std::uint32_t ecx = 0;
        std::uint32_t edi = 0;
        std::vector<std::uint32_t> stack;
        stack.reserve(8);

        for(const Instruction &instruction : Code) {
            switch(instruction.Code) {
                case Op::Nop: break;
                case Op::MovEdiArg: edi = hash; break;
                case Op::PushEbx: stack.push_back(ebx); break;
                case Op::PopEbx:
                    if(stack.empty()) throw std::runtime_error("Cx stack underflow");
                    ebx = stack.back();
                    stack.pop_back();
                    break;
                case Op::PushEcx: stack.push_back(ecx); break;
                case Op::PopEcx:
                    if(stack.empty()) throw std::runtime_error("Cx stack underflow");
                    ecx = stack.back();
                    stack.pop_back();
                    break;
                case Op::MovEaxEbx: eax = ebx; break;
                case Op::MovEbxEax: ebx = eax; break;
                case Op::MovEcxEbx: ecx = ebx; break;
                case Op::MovEaxEdi: eax = edi; break;
                case Op::MovEaxIndirect:
                    if(eax >= ControlBlock.size())
                        throw std::runtime_error("Cx control block index overflow");
                    eax = ~ControlBlock[eax];
                    break;
                case Op::AddEaxEbx: eax += ebx; break;
                case Op::SubEaxEbx: eax -= ebx; break;
                case Op::MulEaxEbx: eax *= ebx; break;
                case Op::AndEcx0f: ecx &= 0x0fu; break;
                case Op::ShrEbx1: ebx >>= 1u; break;
                case Op::ShlEax1: eax <<= 1u; break;
                case Op::ShrEaxCl: eax >>= (ecx & 31u); break;
                case Op::ShlEaxCl: eax <<= (ecx & 31u); break;
                case Op::OrEaxEbx: eax |= ebx; break;
                case Op::NotEax: eax = ~eax; break;
                case Op::NegEax: eax = 0u - eax; break;
                case Op::DecEax: --eax; break;
                case Op::IncEax: ++eax; break;
                case Op::MovEaxImmediate: eax = instruction.Immediate; break;
                case Op::AndEbxImmediate: ebx &= instruction.Immediate; break;
                case Op::AndEaxImmediate: eax &= instruction.Immediate; break;
                case Op::XorEaxImmediate: eax ^= instruction.Immediate; break;
                case Op::AddEaxImmediate: eax += instruction.Immediate; break;
                case Op::SubEaxImmediate: eax -= instruction.Immediate; break;
                case Op::Ret:
                    if(!stack.empty())
                        throw std::runtime_error("Cx stack imbalance");
                    return eax;
            }
        }
        throw std::runtime_error("Cx program has no return instruction");
    }
};

class CxDecoder {
    const CxScheme &Scheme;
    std::array<CxProgram, 128> Programs;

    bool EmitProlog(CxProgram &program) const {
        switch(Scheme.PrologOrder[program.GetRandom() % 3u]) {
            case 2: {
                if(!program.EmitNop(5)) return false;
                if(!program.Emit(Op::MovEaxImmediate, 2)) return false;
                const std::uint32_t index = program.GetRandom() & 0x3ffu;
                return program.EmitImmediateData(index) &&
                       program.Emit(Op::MovEaxIndirect, 0);
            }
            case 1:
                return program.Emit(Op::MovEaxEdi, 2);
            case 0:
                return program.EmitWithRandom(Op::MovEaxImmediate);
        }
        return false;
    }

    bool EmitEvenBranch(CxProgram &program) const {
        switch(Scheme.EvenBranchOrder[program.GetRandom() & 7u]) {
            case 0: return program.Emit(Op::NotEax, 2);
            case 1: return program.Emit(Op::DecEax);
            case 2: return program.Emit(Op::NegEax, 2);
            case 3: return program.Emit(Op::IncEax);
            case 4:
                return program.EmitNop(5) &&
                       program.EmitWithImmediate(Op::AndEaxImmediate, 0x3ffu) &&
                       program.Emit(Op::MovEaxIndirect, 3);
            case 5:
                return program.Emit(Op::PushEbx) &&
                       program.Emit(Op::MovEbxEax, 2) &&
                       program.EmitWithImmediate(Op::AndEbxImmediate, 0xaaaaaaaau, 2) &&
                       program.EmitWithImmediate(Op::AndEaxImmediate, 0x55555555u) &&
                       program.Emit(Op::ShrEbx1, 2) &&
                       program.Emit(Op::ShlEax1, 2) &&
                       program.Emit(Op::OrEaxEbx, 2) &&
                       program.Emit(Op::PopEbx);
            case 6:
                return program.EmitWithRandom(Op::XorEaxImmediate);
            case 7: {
                const bool add = (program.GetRandom() & 1u) != 0;
                return program.EmitWithRandom(add ? Op::AddEaxImmediate
                                                  : Op::SubEaxImmediate);
            }
        }
        return false;
    }

    bool EmitOddBranch(CxProgram &program) const {
        switch(Scheme.OddBranchOrder[program.GetRandom() % 6u]) {
            case 0:
                return program.Emit(Op::PushEcx) &&
                       program.Emit(Op::MovEcxEbx, 2) &&
                       program.Emit(Op::AndEcx0f, 3) &&
                       program.Emit(Op::ShrEaxCl, 2) &&
                       program.Emit(Op::PopEcx);
            case 1:
                return program.Emit(Op::PushEcx) &&
                       program.Emit(Op::MovEcxEbx, 2) &&
                       program.Emit(Op::AndEcx0f, 3) &&
                       program.Emit(Op::ShlEaxCl, 2) &&
                       program.Emit(Op::PopEcx);
            case 2: return program.Emit(Op::AddEaxEbx, 2);
            case 3:
                return program.Emit(Op::NegEax, 2) &&
                       program.Emit(Op::AddEaxEbx, 2);
            case 4: return program.Emit(Op::MulEaxEbx, 3);
            case 5: return program.Emit(Op::SubEaxEbx, 2);
        }
        return false;
    }

    bool EmitBody2(CxProgram &program, int stage) const {
        if(stage == 1)
            return EmitProlog(program);
        const bool body = (program.GetRandom() & 1u) != 0
                              ? EmitBody(program, stage - 1)
                              : EmitBody2(program, stage - 1);
        return body && EmitEvenBranch(program);
    }

    bool EmitBody(CxProgram &program, int stage) const {
        if(stage == 1)
            return EmitProlog(program);
        if(!program.Emit(Op::PushEbx))
            return false;
        if((program.GetRandom() & 1u) != 0) {
            if(!EmitBody(program, stage - 1)) return false;
        } else if(!EmitBody2(program, stage - 1)) {
            return false;
        }
        if(!program.Emit(Op::MovEbxEax, 2))
            return false;
        if((program.GetRandom() & 1u) != 0) {
            if(!EmitBody(program, stage - 1)) return false;
        } else if(!EmitBody2(program, stage - 1)) {
            return false;
        }
        return EmitOddBranch(program) && program.Emit(Op::PopEbx);
    }

    bool EmitCode(CxProgram &program, int stage) const {
        return program.EmitNop(5) &&
               program.Emit(Op::MovEdiArg, 4) &&
               EmitBody(program, stage) &&
               program.EmitNop(5) &&
               program.Emit(Op::Ret);
    }

    CxProgram GenerateProgram(std::uint32_t seed) const {
        CxProgram program(seed, Scheme.ControlBlock);
        for(int stage = 5; stage > 0; --stage) {
            if(EmitCode(program, stage))
                return program;
            // Clearing the generated instructions intentionally preserves the
            // random state, matching the original Cx virtual machine.
            program.Clear();
        }
        throw std::runtime_error("Cx program exceeds bytecode limit");
    }

    std::pair<std::uint32_t, std::uint32_t>
    ExecuteXCode(std::uint32_t hash) const {
        const std::uint32_t seed = hash & 0x7fu;
        hash >>= 7u;
        return { Programs[seed].Execute(hash),
                 Programs[seed].Execute(~hash) };
    }

    void Decode(std::uint32_t key, std::uint64_t offset,
                std::uint8_t *buffer, std::uint32_t size) const {
        const auto result = ExecuteXCode(key);
        std::uint32_t key1 = result.second >> 16u;
        std::uint32_t key2 = result.second & 0xffffu;
        std::uint8_t key3 = static_cast<std::uint8_t>(result.first);
        if(key1 == key2) ++key2;
        if(key3 == 0) key3 = 1;

        const std::uint64_t end = offset + size;
        if(key2 >= offset && key2 < end)
            buffer[key2 - offset] ^= static_cast<std::uint8_t>(result.first >> 16u);
        if(key1 >= offset && key1 < end)
            buffer[key1 - offset] ^= static_cast<std::uint8_t>(result.first >> 8u);
        for(std::uint32_t i = 0; i < size; ++i)
            buffer[i] ^= key3;
    }

public:
    explicit CxDecoder(const CxScheme &scheme) :
        Scheme(scheme),
        Programs{ GenerateProgram(0), GenerateProgram(1), GenerateProgram(2),
                  GenerateProgram(3), GenerateProgram(4), GenerateProgram(5),
                  GenerateProgram(6), GenerateProgram(7), GenerateProgram(8),
                  GenerateProgram(9), GenerateProgram(10), GenerateProgram(11),
                  GenerateProgram(12), GenerateProgram(13), GenerateProgram(14),
                  GenerateProgram(15), GenerateProgram(16), GenerateProgram(17),
                  GenerateProgram(18), GenerateProgram(19), GenerateProgram(20),
                  GenerateProgram(21), GenerateProgram(22), GenerateProgram(23),
                  GenerateProgram(24), GenerateProgram(25), GenerateProgram(26),
                  GenerateProgram(27), GenerateProgram(28), GenerateProgram(29),
                  GenerateProgram(30), GenerateProgram(31), GenerateProgram(32),
                  GenerateProgram(33), GenerateProgram(34), GenerateProgram(35),
                  GenerateProgram(36), GenerateProgram(37), GenerateProgram(38),
                  GenerateProgram(39), GenerateProgram(40), GenerateProgram(41),
                  GenerateProgram(42), GenerateProgram(43), GenerateProgram(44),
                  GenerateProgram(45), GenerateProgram(46), GenerateProgram(47),
                  GenerateProgram(48), GenerateProgram(49), GenerateProgram(50),
                  GenerateProgram(51), GenerateProgram(52), GenerateProgram(53),
                  GenerateProgram(54), GenerateProgram(55), GenerateProgram(56),
                  GenerateProgram(57), GenerateProgram(58), GenerateProgram(59),
                  GenerateProgram(60), GenerateProgram(61), GenerateProgram(62),
                  GenerateProgram(63), GenerateProgram(64), GenerateProgram(65),
                  GenerateProgram(66), GenerateProgram(67), GenerateProgram(68),
                  GenerateProgram(69), GenerateProgram(70), GenerateProgram(71),
                  GenerateProgram(72), GenerateProgram(73), GenerateProgram(74),
                  GenerateProgram(75), GenerateProgram(76), GenerateProgram(77),
                  GenerateProgram(78), GenerateProgram(79), GenerateProgram(80),
                  GenerateProgram(81), GenerateProgram(82), GenerateProgram(83),
                  GenerateProgram(84), GenerateProgram(85), GenerateProgram(86),
                  GenerateProgram(87), GenerateProgram(88), GenerateProgram(89),
                  GenerateProgram(90), GenerateProgram(91), GenerateProgram(92),
                  GenerateProgram(93), GenerateProgram(94), GenerateProgram(95),
                  GenerateProgram(96), GenerateProgram(97), GenerateProgram(98),
                  GenerateProgram(99), GenerateProgram(100), GenerateProgram(101),
                  GenerateProgram(102), GenerateProgram(103), GenerateProgram(104),
                  GenerateProgram(105), GenerateProgram(106), GenerateProgram(107),
                  GenerateProgram(108), GenerateProgram(109), GenerateProgram(110),
                  GenerateProgram(111), GenerateProgram(112), GenerateProgram(113),
                  GenerateProgram(114), GenerateProgram(115), GenerateProgram(116),
                  GenerateProgram(117), GenerateProgram(118), GenerateProgram(119),
                  GenerateProgram(120), GenerateProgram(121), GenerateProgram(122),
                  GenerateProgram(123), GenerateProgram(124), GenerateProgram(125),
                  GenerateProgram(126), GenerateProgram(127) } {}

    void Decode(std::uint32_t hash, std::uint64_t offset,
                void *buffer, std::uint32_t size) const {
        auto *bytes = static_cast<std::uint8_t *>(buffer);
        const std::uint64_t baseOffset = (hash & Scheme.Mask) + Scheme.Offset;
        if(offset < baseOffset) {
            const std::uint32_t prefix = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(baseOffset - offset, size));
            Decode(hash, offset, bytes, prefix);
            offset += prefix;
            bytes += prefix;
            size -= prefix;
        }
        if(size != 0) {
            const std::uint32_t key = (hash >> 16u) ^ hash;
            Decode(key, offset, bytes, size);
        }
    }
};

const CxScheme kWagamamaHighSpecOcScheme {
    0xe425cd85u,
    608u,
    645u,
    { 2, 0, 1 },
    { 3, 5, 4, 0, 2, 1 },
    { 7, 0, 1, 4, 5, 2, 3, 6 },
    {
        0x88ef0c28u, 0x9088762fu, 0x28e583c0u, 0x9112bfb8u, 0x3e60703cu, 0x60a3a3cbu, 0x2b23db99u, 0xaffce271u,
        0x188351ffu, 0x0b19ae46u, 0xc14f6eddu, 0xd15ff4b0u, 0xa12136bdu, 0x1b1ac09du, 0x055e0f91u, 0x2dec8f7fu,
        0xd08df479u, 0x6db241e4u, 0x848838bau, 0x493656a9u, 0xf45ea221u, 0xbf89d65eu, 0x9eb3f6e4u, 0x72da9a88u,
        0x0cde3a9fu, 0xb1e21823u, 0x725bdd40u, 0x46836873u, 0x8c63f258u, 0x8045aa42u, 0x26528fa4u, 0xb07a23f0u,
        0x73eb896au, 0xe9c54b23u, 0x48885740u, 0x9fecee6du, 0x13378aebu, 0x605c80f3u, 0x3bbc1cecu, 0xff1f51ecu,
        0x5cdfb0d6u, 0x5c2d8e86u, 0x9c8f13ebu, 0x38d72867u, 0xd143422eu, 0xc4e4a547u, 0x5474e6d0u, 0xd818f222u,
        0xfaf64424u, 0x52e1f92cu, 0x27a75de9u, 0x98efdeacu, 0x5d1eca8fu, 0x762e8e7fu, 0x0af9f42au, 0x4425f34du,
        0x01b0bc9bu, 0x106183c6u, 0x360ee2d3u, 0x131f27adu, 0x381c41d6u, 0x01aa9045u, 0x05b871bbu, 0x07189c6cu,
        0xb7112321u, 0x840e76b3u, 0x87889890u, 0x3e0c6505u, 0x639f5bacu, 0x52d74e6au, 0xb9073a21u, 0xaf4a5fd1u,
        0x797e0ea9u, 0x6ff35aa1u, 0xd5a29014u, 0x486c574eu, 0xe977c4bau, 0x3206356fu, 0x926a454bu, 0x1253faacu,
        0xaae3663bu, 0xf3960cccu, 0x165569ccu, 0x086326a1u, 0xda1dfa92u, 0xe7d30e32u, 0xb701adc2u, 0xb38d0411u,
        0x3c436d51u, 0x2e0ba98fu, 0x9c6e766au, 0x690c9d5eu, 0xba7c4c24u, 0x21f2942du, 0xbff9fe5eu, 0x4f755dcau,
        0x88725e0eu, 0x7e88d074u, 0x8b932db7u, 0x10993365u, 0x872d7f83u, 0x3b07afa6u, 0xc03e81c8u, 0x208a74edu,
        0x9fc2a759u, 0x4257a6d3u, 0x9a39c0d4u, 0xc82cb741u, 0xd6a178d7u, 0xbcbdbcd7u, 0x60dffd3du, 0x045c6341u,
        0x353cfbabu, 0x633ed5ccu, 0xad05b75cu, 0xcc8477d8u, 0xc29b82eau, 0x7832b6d0u, 0x92a1dc52u, 0x66aca571u,
        0xe9aa26fau, 0x8828ac39u, 0xb10a94ffu, 0x28b51fb8u, 0x062f6892u, 0x457805a9u, 0x7a939182u, 0x5ba68108u,
        0x039c3da0u, 0xb2d66cb7u, 0xb90c43d8u, 0x1bf9326bu, 0x71570eaeu, 0xe58f5795u, 0xefb07bb8u, 0xdc5684b3u,
        0x987e378du, 0x90200fc5u, 0x41b4dd3du, 0x0a896c94u, 0x4b36ecdcu, 0xe5cf50d8u, 0x453efbdfu, 0x21e3865cu,
        0x47c6ffcau, 0x7a2c3a85u, 0xcda65844u, 0x3b1a67dbu, 0xf35447c6u, 0x87188557u, 0x1f17ae05u, 0x8c50491bu,
        0xdec9702au, 0xf273dd60u, 0x1093db89u, 0x267cd1d0u, 0x28c02eabu, 0x08aeb54du, 0x6bdb7383u, 0x73ce37bfu,
        0xaf3fe596u, 0xff9f3450u, 0x2b6991a2u, 0xc13b4ca2u, 0x618dc927u, 0x24e83828u, 0xb9aa03e9u, 0xa229a8e0u,
        0xf5df996eu, 0xd671a88bu, 0x9e3e5b2du, 0x609c6376u, 0xf7b2c4f4u, 0xa6769039u, 0x2280d3f0u, 0xacacb9a9u,
        0xb2587d1fu, 0x3a40d6cau, 0x405a0867u, 0xbc79bf9fu, 0x30962f1fu, 0x34d19ee7u, 0xb2cde623u, 0x4c945843u,
        0x511a75dbu, 0xfe6c3e04u, 0x423e708eu, 0x4cf0238cu, 0x78493789u, 0x090441a2u, 0x71ac6958u, 0xe6434999u,
        0x5a8fdc9cu, 0x7f35f8d6u, 0x1022b6d6u, 0xf69a13c0u, 0x3a395c8eu, 0x62698832u, 0xfb95702au, 0x7cc29293u,
        0xb18ed1f7u, 0xf4b88e7au, 0x9c325f5du, 0xef5240ddu, 0xf127d379u, 0xdc424fb7u, 0x74d3c4d4u, 0x0ee6187fu,
        0x96292a62u, 0xf37912f5u, 0xad486bbcu, 0xe48acfceu, 0xc08b9ac2u, 0x7ad14d82u, 0xf5844f28u, 0xb0cd73a0u,
        0x96426536u, 0xcc569a38u, 0x45eed6aeu, 0x90a3f6f4u, 0x8f08017bu, 0x59084c3bu, 0x27fed2d0u, 0xa98f06abu,
        0xfe5dcec7u, 0x6d6d46f6u, 0xdcde0dd4u, 0xb429ac9bu, 0xb2621d94u, 0x98e786f9u, 0x7465d20bu, 0x9df0f13fu,
        0x08d7bd1bu, 0x78a7298bu, 0x8b638a7bu, 0xf4797289u, 0xd681079bu, 0x40ebf991u, 0xa4113d5bu, 0x6b453c3fu,
        0x92eb930bu, 0x5763b40du, 0x0dbd7bc9u, 0x9768dcb1u, 0x948510dau, 0x0b21b362u, 0xb84b70c8u, 0xd3101c98u,
        0xd4ff9019u, 0x8b4df62cu, 0x68603795u, 0xb86a0546u, 0x9dec140fu, 0x57e5dde6u, 0xa75c49d7u, 0x4b05decfu,
        0x3c93d93cu, 0x7804a38au, 0xa40dfb10u, 0x86d23d15u, 0x3c78dfd6u, 0x659703bau, 0x0cd1ae93u, 0x7191abb6u,
        0x7aaaa8c4u, 0x41cd4c09u, 0x757a7677u, 0x2c32d827u, 0xb3eaeed8u, 0xe40ad4a8u, 0x3cbb1f20u, 0x31c7c57au,
        0x8a7f38ceu, 0xed5ef18du, 0x325158f2u, 0xc309461au, 0xf16704eeu, 0x2120e507u, 0xf115102fu, 0x9a5c585au,
        0x0a48f271u, 0x2370ecd9u, 0x664d3a0bu, 0x67e26828u, 0x6c927124u, 0x4bfe7b0eu, 0xf90206e0u, 0xfffacb1eu,
        0xd193fdaau, 0x7f467f18u, 0xdaa241a7u, 0xf89872f5u, 0xc65feb03u, 0xa12a22b7u, 0x9870992au, 0x2b9b02f6u,
        0x74571ec8u, 0xaa931138u, 0x776677a5u, 0x5eb88a9eu, 0x564770a4u, 0x5477a48cu, 0xe2864b6fu, 0x63eaf347u,
        0x7243e24du, 0x7a39be78u, 0xb62eaa8au, 0x82dadef4u, 0xe0d9a6f3u, 0xdbaf169eu, 0x98e94b9eu, 0x289f1289u,
        0x480c9e5fu, 0x92feccf4u, 0x55c50927u, 0x7719ad87u, 0xf847e318u, 0x3227f895u, 0x232faca2u, 0xabe97109u,
        0x0229ddc3u, 0xbb155f71u, 0xd4873e2fu, 0xbe1721b1u, 0x2a7475fbu, 0xff5cc7b9u, 0x4e4d99dau, 0x47bc7c5bu,
        0x03c8b6b5u, 0x608e3760u, 0x50d18adeu, 0xcafb74e0u, 0xa07e2ddau, 0x20df537bu, 0x2d667e45u, 0x7fa584e5u,
        0xc728339du, 0x6481236du, 0x06ecc031u, 0xf682d9f3u, 0x7a434867u, 0x3be455b1u, 0xafc38707u, 0xc1275774u,
        0x5685c85cu, 0x6d320db3u, 0x861814aeu, 0x863b0639u, 0x08c11415u, 0x934af3fbu, 0x70ad6803u, 0x38efcddau,
        0x24f5f70cu, 0xbfb9362cu, 0x63e8a7bau, 0x3f82a2afu, 0x61be9a01u, 0xc41ab699u, 0x62bdcd0cu, 0xfe645407u,
        0xb3af7383u, 0xe4231395u, 0x63332f25u, 0x36e0f48du, 0x0ae24c62u, 0x74993e59u, 0x9a9eaf80u, 0xe2d94fe2u,
        0xede2ab3cu, 0x71c645abu, 0xdd80292eu, 0xbbf8b41eu, 0xf08c9d28u, 0x236ca2b1u, 0x03104546u, 0xa0d484feu,
        0x8f8e5f70u, 0xcf2cbe85u, 0xf0dff382u, 0x7b9cb79au, 0x0db527c4u, 0x6eb2deb2u, 0x44ff66dau, 0xeff3f260u,
        0x5c9b1b4eu, 0x84f4f882u, 0xd75f8926u, 0xf0e458ffu, 0x5a2f2297u, 0xb6752680u, 0x01e68d61u, 0xfdb1ed34u,
        0x102a57cau, 0x613e4c6au, 0x865492ddu, 0x4a3cd133u, 0x234c6434u, 0x2c3d1ff9u, 0xb51fe450u, 0x1fbef519u,
        0x71451b54u, 0x8207e6c5u, 0xa5f8cc38u, 0xe7cb20bdu, 0x57a22635u, 0x678747dcu, 0xf1c8f449u, 0x4f6b2cfeu,
        0x4d98cd08u, 0xfb27811du, 0x79a6e1f5u, 0xa4b28b7bu, 0x83b2c3b2u, 0x5be7ab15u, 0x7d25ca83u, 0x69d8698au,
        0xb9c06418u, 0x5c7a9431u, 0x6d0b6badu, 0x35db97aau, 0xb12d4be7u, 0x9b638469u, 0xd68cde78u, 0x549ed8b6u,
        0xb86e63b2u, 0xd1cc9572u, 0x4e5edde0u, 0xeb495bc5u, 0x5a756e9du, 0xcd8aff89u, 0x7e437410u, 0x98d6cc9fu,
        0xd4831669u, 0x63fe871au, 0x1aa6756au, 0x48dc4a0du, 0x7be6a4bbu, 0xf4d3ee8eu, 0x1a0e1f97u, 0x15f47a51u,
        0x347f8ff4u, 0xa0085b1eu, 0x97cdd91au, 0x1567b165u, 0xb0bd09e8u, 0x04ff3f09u, 0xaf8fb127u, 0x91417fd4u,
        0x31cf179cu, 0xf0bb5a84u, 0xce94bf73u, 0x76fe1648u, 0xecc0c291u, 0xad588dd4u, 0xfde074fcu, 0x518d0530u,
        0x70aa275bu, 0x345f03a8u, 0x7788e3aeu, 0x3f659391u, 0x3547cdd3u, 0xb0ff32dau, 0x72c027f7u, 0xfad52f13u,
        0x1060d3b6u, 0x89d706cau, 0x8537e40au, 0xc5cd4692u, 0x9f419b1cu, 0x24536fa5u, 0xa3f4eed9u, 0xec73aa31u,
        0x2b6103ddu, 0x7e5c6172u, 0x76ccca8au, 0xccce9fbdu, 0xe8be1644u, 0x944d16b6u, 0x534f8be4u, 0xa980678cu,
        0x3b700fd7u, 0x33be769au, 0x1be9116au, 0xd09b9f39u, 0xf2719640u, 0xc6224b16u, 0xfb0401dau, 0xaff2edb1u,
        0x5e7eab5eu, 0x477d7aa6u, 0x4cf55e41u, 0x0315ee8au, 0xc1a8497eu, 0xf4feb34cu, 0xaa4f7a2cu, 0x29d3073eu,
        0x018d8d8du, 0x8261eec1u, 0xea3097c8u, 0xd28a5623u, 0x0ef880c7u, 0xe9ff78f6u, 0xe7e5b0eeu, 0x0df84ec4u,
        0x31c36547u, 0x23f21339u, 0xc1f5b8cbu, 0xab6903d8u, 0x9d0b03dfu, 0x894b43b5u, 0x679bff17u, 0x4637e8e8u,
        0xfb5f5433u, 0x20e3b589u, 0xeb348773u, 0xaaf56218u, 0x5a1ec389u, 0xdac6259du, 0x0995936cu, 0x3266adedu,
        0x8e3d072cu, 0xfd3272c8u, 0x4416b1e2u, 0xbb23dbabu, 0x63ffd8f4u, 0x6c0fe806u, 0xed64ba4eu, 0x0d8f73dcu,
        0x0f65c62bu, 0xf9ab4de7u, 0x97b4ea66u, 0xaff9514cu, 0xf5f268b0u, 0xb4d72789u, 0xacbbcb6au, 0x2eb3fc77u,
        0xcda423dcu, 0x7f71f7aau, 0xff1a9cbau, 0x539b9519u, 0x981b3ec8u, 0x403f916cu, 0xc7957159u, 0xc974964eu,
        0xc8eeb234u, 0xa9bd0efbu, 0xac5b417fu, 0x3c9e7c8au, 0x2a0ba194u, 0x383d28aeu, 0xec24a8c8u, 0x3601ad1du,
        0x9cc6f8ceu, 0xef5f19adu, 0x439863a3u, 0xb08f7ed1u, 0xb473df01u, 0x2a747314u, 0xc4f8cf22u, 0xc9f67f92u,
        0x662992feu, 0xc08788e7u, 0xbe536193u, 0xc7612da8u, 0x79c1cc0fu, 0x2a9fc435u, 0xe90a7965u, 0xb7d00d58u,
        0x618d7eedu, 0xf330ad4cu, 0x67b19988u, 0xa1dcc83fu, 0x43797471u, 0xb3963ec9u, 0x97eacd70u, 0x0279d9fbu,
        0x370f1210u, 0x97065b66u, 0xb89c39aau, 0xccff62bdu, 0x5a0ce4b8u, 0xdde623d2u, 0xf1eebf0au, 0xea8a6fc8u,
        0xb622a6bbu, 0x5accc8bdu, 0x1030f3bau, 0xa8636bd8u, 0x6223e071u, 0xecc1caadu, 0xa49bd6feu, 0x9ad478afu,
        0x1dc88dc4u, 0x43e2b289u, 0x0fd2db06u, 0x343583ffu, 0xed717fb4u, 0xbfccd036u, 0x1995075au, 0x16a0bb2au,
        0x4a1c5a3fu, 0x2258f2a8u, 0x85422a99u, 0x3815dd22u, 0x01d5912cu, 0x565e2ff1u, 0x9bb31f73u, 0x7b4761fau,
        0x4326a850u, 0x80ce37a7u, 0xac243fe1u, 0x57ae46ddu, 0xc9e4c0cfu, 0x47131a73u, 0x1878ca05u, 0xe1cda9f3u,
        0x5522cbbcu, 0xe91b5a2au, 0xc0724d78u, 0x9c7c37a7u, 0x138eab02u, 0x967988cfu, 0x8137d36bu, 0xbcba7ffau,
        0xf3d19836u, 0x4a21cdcbu, 0x5e828c03u, 0xf87f4a87u, 0x24780142u, 0x3a1c54e4u, 0xd0aa981fu, 0x6d6796c2u,
        0x2db6a36du, 0xda0e5ca3u, 0x8c59436fu, 0x76540d7au, 0x611d83a8u, 0xd3c9b06bu, 0x7dee7d36u, 0xd130604fu,
        0xc6664e12u, 0xb1987eedu, 0x327ba0a1u, 0xe7f2857au, 0xe9379c2du, 0xb4992be6u, 0x27cd339fu, 0xa7e50437u,
        0x84671740u, 0x3e180ae0u, 0x9f7f2948u, 0xc479f14fu, 0x5c7c5205u, 0x45843b47u, 0x0c5a6f6eu, 0x3b9b39eau,
        0xb5614736u, 0xb2e839a2u, 0xe20855bdu, 0x6335a979u, 0x0b612bf5u, 0xcd1fb66fu, 0x1819d7f4u, 0x6f67d508u,
        0x26d106a6u, 0xe96e503cu, 0x38c3c809u, 0xd590d354u, 0xe16f80b2u, 0x76aa8601u, 0x73d13c50u, 0x19943955u,
        0x46e9a7cbu, 0x8efeecc8u, 0xc227438fu, 0x606138aeu, 0x177b95e6u, 0xfa035355u, 0xf543282eu, 0x69eea92bu,
        0xdaab123eu, 0xd1b44b19u, 0xd725c4c0u, 0x8f0665c5u, 0x2be40c55u, 0x5663b6fbu, 0xff592ee9u, 0x65f387a7u,
        0x0545eb1cu, 0x9b114359u, 0x73f211c8u, 0x172717fbu, 0xfe7f1a12u, 0x7c9c34e9u, 0xb579710eu, 0x22f7cfe8u,
        0x81ac4879u, 0x0c2adf16u, 0x66ad60b5u, 0x0ca75eeau, 0xaca590ccu, 0xba9f37d2u, 0x3b4f4680u, 0xaeff2669u,
        0xeaa06dc2u, 0x8d8e425au, 0x6095ff4fu, 0x96260b6fu, 0xe28c9b05u, 0x894b92f8u, 0xa5bdc12fu, 0x0bd2afb7u,
        0x8c29d1f8u, 0x773f3c95u, 0x1786432du, 0x579548b5u, 0x8b617f4eu, 0x618b1d6eu, 0x3b1e0896u, 0x080cd76du,
        0xdefec50eu, 0xd8a3d554u, 0xa0ee1890u, 0x263e54b3u, 0xed692052u, 0xe4affef6u, 0x785f8aceu, 0x1c2e1e5eu,
        0xe66a9f38u, 0x6ac55163u, 0x7e57b419u, 0xc57a6191u, 0xc5b6e388u, 0x8357c18bu, 0x367d5cd8u, 0xf4a4bed2u,
        0x8f20ab83u, 0x999fff9fu, 0xce10f111u, 0x7aec22ffu, 0x2f685bccu, 0xf9195db4u, 0xd319cd56u, 0xc6681394u,
        0x5b6c05bbu, 0x208c8fa8u, 0x5dbd80bcu, 0xe0cb014eu, 0x23cbd797u, 0xd2a06e25u, 0x7c15ca5fu, 0xe68ac133u,
        0xaf5c504du, 0x63db5e62u, 0xb96e6692u, 0x45c43e64u, 0xff2b50c0u, 0x135755b8u, 0x11851fb6u, 0xb2ca31c7u,
        0x9bdcad2au, 0xb906a535u, 0x5ea022fcu, 0x39fe89f6u, 0xe1379935u, 0x0b6afab8u, 0x5df93a4cu, 0x6d8d314fu,
        0x49afe6f3u, 0x192a04bfu, 0x6b4e2123u, 0xfdfab003u, 0xff3d751eu, 0x3e581551u, 0x22bc85f5u, 0xe4f609d6u,
        0x5bcabab9u, 0x7154b239u, 0x4f493e18u, 0xe9fe6edbu, 0x9cd71feau, 0xf20ad450u, 0x9972771eu, 0xf375e097u,
        0x96cccc81u, 0x2f2109c4u, 0x81c760d3u, 0x80e52765u, 0xb619c666u, 0x949d8aecu, 0xfb60672bu, 0x5e6a52aeu,
        0x4bb14dbbu, 0x51560a4au, 0xa2933c4eu, 0x0f195545u, 0x08af905cu, 0x1c5103a3u, 0x7383dc63u, 0xad0703a9u,
        0xabf50a25u, 0xfdaa5cfcu, 0xd37a0bdbu, 0xa204fe64u, 0x0b993e46u, 0x69f8b268u, 0xe1221b31u, 0xa15d4b2bu,
        0x08dd4c62u, 0xb913649au, 0x871b2351u, 0xb93f1ed7u, 0xd35c8c86u, 0x700fbac8u, 0x9413ff6eu, 0xa4d46810u,
        0x3a861055u, 0x42959761u, 0xb0adb9bcu, 0xbf188cdeu, 0xf52c91eau, 0x3efcf00bu, 0xeea74273u, 0x2b68d2efu,
        0xb2d606c3u, 0xc6213399u, 0x4705c10bu, 0x579e727cu, 0x41e47838u, 0xf77e7059u, 0x79a0bc78u, 0x087ed1a6u,
        0x81f5da67u, 0xb9c42afdu, 0x85e685c8u, 0x864e8d54u, 0x95ad720cu, 0x5a85fe4du, 0xd3992bbcu, 0x6013fd56u,
        0xfe2cc0f1u, 0x37139ac2u, 0xe41d5cfeu, 0x4305fc8eu, 0x2ae892b4u, 0x12d0bc11u, 0x28f59433u, 0x211421ccu,
        0x40b9e624u, 0x46bfab7bu, 0x479c6df8u, 0xcc2c1c3bu, 0xf252f409u, 0x8fec03cfu, 0x2465d61fu, 0x1e3c4581u,
        0x3fa155ddu, 0x1dbb949fu, 0x72a512a2u, 0x3b5e1f80u, 0x80b53f3cu, 0x5153c442u, 0x4665b6d4u, 0x8fe1db1au,
        0x5d16f817u, 0x937f2b5cu, 0x774650b7u, 0x49f4d32du, 0x9a35f2a9u, 0x6f666f3au, 0xc3f61781u, 0x3f496b86u,
        0x2a7f1757u, 0x66c3412au, 0xf80e9f45u, 0x49a796c9u, 0x1ed58426u, 0x6833fa8bu, 0xd3641e8bu, 0x8a3bd7abu,
        0xa2e5e960u, 0x58509935u, 0xcba40d85u, 0x0a91ad27u, 0x4e2c5a1du, 0xdbdb434eu, 0x56905840u, 0x8fb47880u,
        0xb5cc4e5cu, 0xd4207f90u, 0xa4ae57e3u, 0xc62b47abu, 0x877b18c8u, 0x84852ab6u, 0xfed23fbdu, 0x428c026eu,
        0x4d789edcu, 0x45d608bau, 0x0df3ee6fu, 0xa87a97e0u, 0x24fca95eu, 0x3fb58f7du, 0x02312226u, 0x8a0ea34eu,
        0xe12c3badu, 0x826c72aau, 0xe52227a1u, 0xb5ca656du, 0x083e92a5u, 0x514bb950u, 0x8493a7c6u, 0xe64c6b45u,
        0x4b4506e7u, 0xce3617f4u, 0x06bc1b3bu, 0x332da03au, 0x37c16cf9u, 0x66b5da0fu, 0xb1e5d34eu, 0x445064c7u,
        0x9618c273u, 0x8a6370c2u, 0x1f5820f6u, 0xe431d2c2u, 0x163ce452u, 0x99273a8du, 0xbf18be9bu, 0x6bf9d0a9u,
        0xf27f04e8u, 0x68da68bcu, 0x6d1436abu, 0x5777edb7u, 0x9c031f7cu, 0x177d62e6u, 0xcd8daab8u, 0x30f1ac18u,
        0x74f0feeeu, 0x2e220631u, 0x14cf8badu, 0xfb0ce691u, 0xf2494d27u, 0x96e901edu, 0x5cf4cfc1u, 0x55982328u,
        0x5cbb1019u, 0x29586853u, 0x28bf3563u, 0xaef30a5cu, 0x95081b94u, 0xae8ec211u, 0x07ae6af9u, 0x31c2f02cu,
        0xbd106096u, 0x616fe096u, 0xac59ae73u, 0xe11394d7u, 0x288413dbu, 0x716eaa27u, 0x2dfb644au, 0x82cec6e6u,
        0x39c7fbfeu, 0xeabed2a5u, 0x348ed5f9u, 0xd548f9dcu, 0xe6fdb049u, 0x33a56ca7u, 0xf7dbf05bu, 0x2c0fc547u,
        0x3fda5956u, 0xee9e5ba6u, 0x6889670fu, 0xf13e3abau, 0x3c94f5fbu, 0xdebc1ab8u, 0x1718451du, 0x6b80e9c2u,
        0x211291e7u, 0x15318d62u, 0x8868d3cfu, 0x664aa0c8u, 0x4945933au, 0xbb1516eau, 0xe0755d2bu, 0xb8e58c0au,
        0x3d779ae5u, 0xc5781456u, 0xdd6838aeu, 0x8c457ebcu, 0x290219f4u, 0x4b645992u, 0x0c88c587u, 0xfcae6c67u,
        0x7ab0f6a9u, 0xf1269b2eu, 0x02ab298au, 0x9e6d3819u, 0xeb546fe6u, 0x40f35eebu, 0x7561ac65u, 0x43e26cacu,
        0x7a01e851u, 0x23a51a79u, 0x8b436198u, 0x2fb91ccau, 0x8669562eu, 0xc4d05fd5u, 0xae30bb0eu, 0x332ff702u,
        0x7e836005u, 0xa3c206dau, 0x9e706aebu, 0x14903220u, 0xd410a99cu, 0x63d62fabu, 0xd2779404u, 0xc6fbaddbu,
        0x05515aaeu, 0xf9de4e27u, 0x531c0f3du, 0x538b1949u, 0xb21b2d61u, 0xb16f7022u, 0x16d22761u, 0xe8e9324cu,
    },
};

const CxDecoder &GetWagamamaHighSpecOcDecoder() {
    static const CxDecoder decoder(kWagamamaHighSpecOcScheme);
    return decoder;
}

std::atomic<const CxDecoder *> gActiveDecoder{ nullptr };

} // namespace

bool TVPIsBuiltinXP3CxScheme(std::uint32_t fingerprint) {
    return fingerprint == kWagamamaHighSpecOcScheme.Fingerprint;
}

bool TVPShouldUseBuiltinXP3CxDecoder(std::uint32_t fingerprint,
                                    const void *header,
                                    std::size_t headerSize) {
    if(!TVPIsBuiltinXP3CxScheme(fingerprint))
        return false;

    // Some translated/repacked XP3s retain the protected flag and original
    // hashes after their payload has already been decrypted. The generic
    // startup bytecode therefore has the same fingerprint as the encrypted
    // title, but applying Cx a second time corrupts every script. Validate the
    // stored payload before selecting the scheme.
    static constexpr std::uint8_t kTjsByteCodeHeader[] = {
        'T', 'J', 'S', '2', '1', '0', '0', 0,
    };
    return header == nullptr || headerSize < sizeof(kTjsByteCodeHeader) ||
        std::memcmp(header, kTjsByteCodeHeader,
                    sizeof(kTjsByteCodeHeader)) != 0;
}

bool TVPActivateBuiltinXP3CxDecoder(std::uint32_t fingerprint) {
    if(!TVPIsBuiltinXP3CxScheme(fingerprint))
        return false;
    const CxDecoder *decoder = &GetWagamamaHighSpecOcDecoder();
    gActiveDecoder.store(decoder, std::memory_order_release);
    return true;
}

void TVPResetBuiltinXP3CxDecoder() {
    gActiveDecoder.store(nullptr, std::memory_order_release);
}

bool TVPDecodeBuiltinXP3Cx(std::uint32_t hash, std::uint64_t offset,
                           void *buffer, std::uint32_t size) {
    const CxDecoder *decoder = gActiveDecoder.load(std::memory_order_acquire);
    if(!decoder)
        return false;
    decoder->Decode(hash, offset, buffer, size);
    return true;
}
