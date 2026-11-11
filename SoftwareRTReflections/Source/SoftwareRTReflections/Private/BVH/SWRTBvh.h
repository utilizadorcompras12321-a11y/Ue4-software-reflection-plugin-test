// SoftwareRTReflections - BVH builder (CPU, sem dependencias do Unreal).
//
// Formatos:
//   1) Binario (32 B/no)          - baseline para comparacao
//   2) BVH4 sem compressao (128 B/no)
//   3) BVH4 comprimido   (64 B/no) - AABB dos filhos quantizadas a 8 bits relativas ao no
//
// Este header e deliberadamente independente do motor para poder ser testado
// fora do UE (ver Tools/bvh_test.cpp). Os layouts em memoria TEM de coincidir com
// Shaders/Private/SWRTBvhTraversal.ush.
#pragma once

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace SWRT
{
	struct FAabb
	{
		float Min[3] = { FLT_MAX, FLT_MAX, FLT_MAX };
		float Max[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };

		void Grow(const float* P)
		{
			for (int i = 0; i < 3; ++i) { Min[i] = P[i] < Min[i] ? P[i] : Min[i]; Max[i] = P[i] > Max[i] ? P[i] : Max[i]; }
		}
		void Grow(const FAabb& B)
		{
			for (int i = 0; i < 3; ++i) { Min[i] = B.Min[i] < Min[i] ? B.Min[i] : Min[i]; Max[i] = B.Max[i] > Max[i] ? B.Max[i] : Max[i]; }
		}
		float Area() const
		{
			const float dx = Max[0] - Min[0], dy = Max[1] - Min[1], dz = Max[2] - Min[2];
			if (dx < 0.f) return 0.f;
			return 2.f * (dx * dy + dy * dz + dz * dx);
		}
	};

	// ---------------------------------------------------------------- referencias de filho
	// Referencia de filho (32 bit):
	//   0xFFFFFFFF            : vazio
	//   bit31 = 1             : folha. bits 27..30 = numero de triangulos (1..15), bits 0..26 = primeiro triangulo
	//   bit31 = 0             : indice de no
	static const uint32_t ChildEmpty = 0xFFFFFFFFu;
	static inline bool     RefIsEmpty(uint32_t R) { return R == ChildEmpty; }
	static inline bool     RefIsLeaf(uint32_t R) { return R != ChildEmpty && (R & 0x80000000u) != 0; }
	static inline uint32_t RefLeafCount(uint32_t R) { return (R >> 27) & 0xFu; }
	static inline uint32_t RefLeafFirst(uint32_t R) { return R & 0x07FFFFFFu; }
	static inline uint32_t MakeLeafRef(uint32_t First, uint32_t Count) { return 0x80000000u | (Count << 27) | First; }

	// ---------------------------------------------------------------- formatos GPU
	struct FBinaryNode // 32 B
	{
		float    Min[3];
		uint32_t LeftOrFirst; // interno: indice do filho esquerdo (direito = esquerdo+1). folha: primeiro triangulo
		float    Max[3];
		uint32_t Count;       // 0 = interno
	};
	static_assert(sizeof(FBinaryNode) == 32, "FBinaryNode");

	struct FBvh4Node // 128 B
	{
		float    Lo[4][3];
		float    Hi[4][3];
		uint32_t Child[4];
		uint32_t Pad[4];
	};
	static_assert(sizeof(FBvh4Node) == 128, "FBvh4Node");

	struct FBvh4NodeCompressed // 64 B
	{
		float    P[3];        // origem (minimo das caixas dos filhos)
		uint8_t  Exp[3];      // expoente enviesado (127 = 2^0). escala = 2^(Exp-127)
		uint8_t  Reserved;
		uint32_t Child[4];
		uint8_t  QLo[12];     // [filho*3 + eixo]
		uint8_t  QHi[12];
		uint8_t  Pad[8];
	};
	static_assert(sizeof(FBvh4NodeCompressed) == 64, "FBvh4NodeCompressed");
	// Offsets que SWRTBvhTraversal.ush assume:
	static_assert(offsetof(FBvh4NodeCompressed, P)     == 0,  "layout HLSL: P");
	static_assert(offsetof(FBvh4NodeCompressed, Exp)   == 12, "layout HLSL: Exp");
	static_assert(offsetof(FBvh4NodeCompressed, Child) == 16, "layout HLSL: Child");
	static_assert(offsetof(FBvh4NodeCompressed, QLo)   == 32, "layout HLSL: QLo");
	static_assert(offsetof(FBvh4NodeCompressed, QHi)   == 44, "layout HLSL: QHi");

	struct FBvhBuildSettings
	{
		uint32_t MaxLeafTris = 4;  // <= 15
		uint32_t NumBins     = 16;
	};

	struct FBvhBinary
	{
		std::vector<FBinaryNode> Nodes;     // Nodes[0] = raiz
		std::vector<uint32_t>    TriOrder;  // TriOrder[i] = indice do triangulo original na posicao i
	};

	// ---------------------------------------------------------------- builder binario (SAH com bins)
	class FBinaryBuilder
	{
	public:
		FBinaryBuilder(const float* InPositions, const uint32_t* InIndices, uint32_t InNumTris, const FBvhBuildSettings& S)
			: Pos(InPositions), Idx(InIndices), NumTris(InNumTris), Settings(S)
		{
		}

		void Build(FBvhBinary& Out)
		{
			Out.Nodes.clear();
			Out.TriOrder.resize(NumTris);
			TriBox.resize(NumTris);
			Centroid.resize(NumTris * 3);
			for (uint32_t t = 0; t < NumTris; ++t)
			{
				Out.TriOrder[t] = t;
				FAabb B;
				for (int v = 0; v < 3; ++v) B.Grow(&Pos[Idx[t * 3 + v] * 3]);
				TriBox[t] = B;
				for (int a = 0; a < 3; ++a) Centroid[t * 3 + a] = 0.5f * (B.Min[a] + B.Max[a]);
			}
			if (NumTris == 0) return;
			Out.Nodes.reserve(NumTris);
			Out.Nodes.push_back(FBinaryNode());
			BuildRange(Out, 0, 0, NumTris);
		}

	private:
		const float*    Pos;
		const uint32_t* Idx;
		uint32_t        NumTris;
		FBvhBuildSettings Settings;
		std::vector<FAabb> TriBox;
		std::vector<float> Centroid;

		void BuildRange(FBvhBinary& Out, uint32_t NodeIdx, uint32_t Begin, uint32_t End)
		{
			FAabb Box, CBox;
			for (uint32_t i = Begin; i < End; ++i)
			{
				const uint32_t t = Out.TriOrder[i];
				Box.Grow(TriBox[t]);
				CBox.Grow(&Centroid[t * 3]);
			}
			auto WriteBox = [&](FBinaryNode& N) { for (int a = 0; a < 3; ++a) { N.Min[a] = Box.Min[a]; N.Max[a] = Box.Max[a]; } };

			const uint32_t Count = End - Begin;
			int Axis = 0;
			float Ext = CBox.Max[0] - CBox.Min[0];
			for (int a = 1; a < 3; ++a) { const float e = CBox.Max[a] - CBox.Min[a]; if (e > Ext) { Ext = e; Axis = a; } }

			const bool bDegenerate = (Ext <= 1e-12f); // centroides coincidentes
			bool bMakeLeaf = (Count <= Settings.MaxLeafTris) || (bDegenerate && Count <= 15);
			uint32_t Mid = Begin + Count / 2;

			if (!bMakeLeaf && !bDegenerate)
			{
				const uint32_t NB = Settings.NumBins;
				std::vector<FAabb> BinBox(NB);
				std::vector<uint32_t> BinCnt(NB, 0);
				const float Scale = (float)NB / Ext;
				for (uint32_t i = Begin; i < End; ++i)
				{
					const uint32_t t = Out.TriOrder[i];
					uint32_t b = (uint32_t)((Centroid[t * 3 + Axis] - CBox.Min[Axis]) * Scale);
					if (b >= NB) b = NB - 1;
					BinBox[b].Grow(TriBox[t]);
					BinCnt[b]++;
				}
				std::vector<float> LeftArea(NB - 1), RightArea(NB - 1);
				std::vector<uint32_t> LeftCnt(NB - 1), RightCnt(NB - 1);
				FAabb Acc; uint32_t C = 0;
				for (uint32_t b = 0; b < NB - 1; ++b) { Acc.Grow(BinBox[b]); C += BinCnt[b]; LeftArea[b] = Acc.Area(); LeftCnt[b] = C; }
				Acc = FAabb(); C = 0;
				for (int b = (int)NB - 1; b > 0; --b) { Acc.Grow(BinBox[b]); C += BinCnt[b]; RightArea[b - 1] = Acc.Area(); RightCnt[b - 1] = C; }

				float BestCost = FLT_MAX; uint32_t BestSplit = 0;
				for (uint32_t b = 0; b < NB - 1; ++b)
				{
					if (LeftCnt[b] == 0 || RightCnt[b] == 0) continue;
					const float Cost = LeftArea[b] * LeftCnt[b] + RightArea[b] * RightCnt[b];
					if (Cost < BestCost) { BestCost = Cost; BestSplit = b; }
				}
				// Sem divisao valida (todos os centroides no mesmo bin): folha se couber, senao mediana.
				if (BestCost == FLT_MAX)
				{
					bMakeLeaf = (Count <= 15);
				}
				if (!bMakeLeaf)
				{
					uint32_t* Base = Out.TriOrder.data();
					uint32_t* Split = std::partition(Base + Begin, Base + End, [&](uint32_t t)
					{
						uint32_t b = (uint32_t)((Centroid[t * 3 + Axis] - CBox.Min[Axis]) * Scale);
						if (b >= NB) b = NB - 1;
						return b <= BestSplit;
					});
					Mid = (uint32_t)(Split - Base);
					if (Mid == Begin || Mid == End) Mid = Begin + Count / 2; // degenerado
				}
			}

			if (bMakeLeaf)
			{
				FBinaryNode& N = Out.Nodes[NodeIdx];
				WriteBox(N); N.LeftOrFirst = Begin; N.Count = Count;
				return;
			}

			const uint32_t L = (uint32_t)Out.Nodes.size();
			Out.Nodes.push_back(FBinaryNode());
			Out.Nodes.push_back(FBinaryNode());
			{
				FBinaryNode& N = Out.Nodes[NodeIdx];
				WriteBox(N); N.LeftOrFirst = L; N.Count = 0;
			}
			BuildRange(Out, L, Begin, Mid);
			BuildRange(Out, L + 1, Mid, End);
		}
	};

	// ---------------------------------------------------------------- BVH4 (colapso do binario)
	struct FBvh4
	{
		std::vector<FBvh4Node> Nodes;       // Nodes[0] = raiz
		uint32_t               RootRef = ChildEmpty; // se a raiz for folha, RootRef e uma folha e Nodes esta vazio
	};

	class FBvh4Collapser
	{
	public:
		static void Collapse(const FBvhBinary& Bin, FBvh4& Out)
		{
			Out.Nodes.clear();
			if (Bin.Nodes.empty()) { Out.RootRef = ChildEmpty; return; }
			const FBinaryNode& R = Bin.Nodes[0];
			if (R.Count != 0) { Out.RootRef = MakeLeafRef(R.LeftOrFirst, R.Count); return; }
			Out.Nodes.reserve(Bin.Nodes.size() / 2 + 1);
			Out.RootRef = BuildWide(Bin, Out, 0);
		}

	private:
		static uint32_t BuildWide(const FBvhBinary& Bin, FBvh4& Out, uint32_t BinIdx)
		{
			uint32_t Kids[4]; int NK = 0;
			const FBinaryNode& N = Bin.Nodes[BinIdx];
			Kids[NK++] = N.LeftOrFirst; Kids[NK++] = N.LeftOrFirst + 1;
			while (NK < 4)
			{
				int Best = -1; float BestArea = -1.f;
				for (int i = 0; i < NK; ++i)
				{
					const FBinaryNode& C = Bin.Nodes[Kids[i]];
					if (C.Count != 0) continue;
					FAabb B; std::memcpy(B.Min, C.Min, 12); std::memcpy(B.Max, C.Max, 12);
					const float A = B.Area();
					if (A > BestArea) { BestArea = A; Best = i; }
				}
				if (Best < 0) break;
				const uint32_t Expand = Kids[Best];
				Kids[Best] = Bin.Nodes[Expand].LeftOrFirst;
				Kids[NK++] = Bin.Nodes[Expand].LeftOrFirst + 1;
			}

			const uint32_t Me = (uint32_t)Out.Nodes.size();
			Out.Nodes.push_back(FBvh4Node());
			{
				FBvh4Node& W = Out.Nodes[Me];
				std::memset(&W, 0, sizeof(W));
				for (int i = 0; i < 4; ++i) W.Child[i] = ChildEmpty;
			}
			for (int i = 0; i < NK; ++i)
			{
				const FBinaryNode& C = Bin.Nodes[Kids[i]];
				uint32_t Ref;
				if (C.Count != 0) Ref = MakeLeafRef(C.LeftOrFirst, C.Count);
				else              Ref = BuildWide(Bin, Out, Kids[i]); // pode realocar Out.Nodes
				FBvh4Node& W = Out.Nodes[Me];
				for (int a = 0; a < 3; ++a) { W.Lo[i][a] = C.Min[a]; W.Hi[i][a] = C.Max[a]; }
				W.Child[i] = Ref;
			}
			return Me;
		}
	};

	// ---------------------------------------------------------------- compressao
	struct FBvh4Compressed
	{
		std::vector<FBvh4NodeCompressed> Nodes;
		uint32_t                         RootRef = ChildEmpty;
	};

	class FBvh4Compressor
	{
	public:
		static void Compress(const FBvh4& In, FBvh4Compressed& Out)
		{
			Out.RootRef = In.RootRef;
			Out.Nodes.resize(In.Nodes.size());
			for (size_t n = 0; n < In.Nodes.size(); ++n)
			{
				const FBvh4Node& W = In.Nodes[n];
				FBvh4NodeCompressed& C = Out.Nodes[n];
				std::memset(&C, 0, sizeof(C));

				float P[3] = { FLT_MAX, FLT_MAX, FLT_MAX }, Q[3] = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
				for (int i = 0; i < 4; ++i)
				{
					if (RefIsEmpty(W.Child[i])) continue;
					for (int a = 0; a < 3; ++a) { P[a] = W.Lo[i][a] < P[a] ? W.Lo[i][a] : P[a]; Q[a] = W.Hi[i][a] > Q[a] ? W.Hi[i][a] : Q[a]; }
				}
				for (int a = 0; a < 3; ++a)
				{
					if (P[a] > Q[a]) { P[a] = 0.f; Q[a] = 0.f; }
					C.P[a] = P[a];
					const float Ext = Q[a] - P[a];
					int E = (Ext > 0.f) ? (int)std::ceil(std::log2(Ext / 255.0f)) : -126;
					int Eb = E + 127; Eb = Eb < 1 ? 1 : (Eb > 254 ? 254 : Eb);
					// garantir que 255 * escala cobre a extensao (arredondamento do log2)
					while (Eb < 254 && 255.0f * ScaleFromExp((uint8_t)Eb) < Ext) ++Eb;
					C.Exp[a] = (uint8_t)Eb;
				}
				for (int i = 0; i < 4; ++i)
				{
					C.Child[i] = W.Child[i];
					if (RefIsEmpty(W.Child[i]))
					{
						for (int a = 0; a < 3; ++a) { C.QLo[i * 3 + a] = 255; C.QHi[i * 3 + a] = 0; }
						continue;
					}
					for (int a = 0; a < 3; ++a)
					{
						const float S = ScaleFromExp(C.Exp[a]);
						float Lo = std::floor((W.Lo[i][a] - C.P[a]) / S);
						float Hi = std::ceil ((W.Hi[i][a] - C.P[a]) / S);
						Lo = Lo < 0.f ? 0.f : (Lo > 255.f ? 255.f : Lo);
						Hi = Hi < 0.f ? 0.f : (Hi > 255.f ? 255.f : Hi);
						C.QLo[i * 3 + a] = (uint8_t)Lo;
						C.QHi[i * 3 + a] = (uint8_t)Hi;
					}
				}
			}
		}

		static float ScaleFromExp(uint8_t Eb)
		{
			uint32_t Bits = (uint32_t)Eb << 23; float F; std::memcpy(&F, &Bits, 4); return F;
		}
		static void DecodeBox(const FBvh4NodeCompressed& C, int Child, float* OutLo, float* OutHi)
		{
			for (int a = 0; a < 3; ++a)
			{
				const float S = ScaleFromExp(C.Exp[a]);
				OutLo[a] = C.P[a] + (float)C.QLo[Child * 3 + a] * S;
				OutHi[a] = C.P[a] + (float)C.QHi[Child * 3 + a] * S;
			}
		}
	};

	// ---------------------------------------------------------------- travessia de referencia (CPU)
	struct FRay { float O[3]; float D[3]; float TMin; float TMax; };
	struct FHit { float T = FLT_MAX; uint32_t Tri = 0xFFFFFFFFu; };
	struct FTraversalStats { uint64_t NodesVisited = 0; uint64_t TriTests = 0; };

	static inline bool RayTri(const FRay& R, const float* A, const float* B, const float* C, float& OutT)
	{
		float E1[3] = { B[0] - A[0], B[1] - A[1], B[2] - A[2] };
		float E2[3] = { C[0] - A[0], C[1] - A[1], C[2] - A[2] };
		float P[3]  = { R.D[1] * E2[2] - R.D[2] * E2[1], R.D[2] * E2[0] - R.D[0] * E2[2], R.D[0] * E2[1] - R.D[1] * E2[0] };
		const float Det = E1[0] * P[0] + E1[1] * P[1] + E1[2] * P[2];
		if (std::fabs(Det) < 1e-12f) return false;
		const float Inv = 1.f / Det;
		float T[3] = { R.O[0] - A[0], R.O[1] - A[1], R.O[2] - A[2] };
		const float U = (T[0] * P[0] + T[1] * P[1] + T[2] * P[2]) * Inv;
		if (U < 0.f || U > 1.f) return false;
		float Qv[3] = { T[1] * E1[2] - T[2] * E1[1], T[2] * E1[0] - T[0] * E1[2], T[0] * E1[1] - T[1] * E1[0] };
		const float V = (R.D[0] * Qv[0] + R.D[1] * Qv[1] + R.D[2] * Qv[2]) * Inv;
		if (V < 0.f || U + V > 1.f) return false;
		const float Tt = (E2[0] * Qv[0] + E2[1] * Qv[1] + E2[2] * Qv[2]) * Inv;
		if (Tt < R.TMin || Tt > R.TMax) return false;
		OutT = Tt; return true;
	}

	static inline bool RayBox(const float* O, const float* InvD, const float* Lo, const float* Hi, float TMax, float& OutTNear)
	{
		float T0 = 0.f, T1 = TMax;
		for (int a = 0; a < 3; ++a)
		{
			float Ta = (Lo[a] - O[a]) * InvD[a];
			float Tb = (Hi[a] - O[a]) * InvD[a];
			if (Ta > Tb) { const float X = Ta; Ta = Tb; Tb = X; }
			T0 = Ta > T0 ? Ta : T0;
			T1 = Tb < T1 ? Tb : T1;
			if (T0 > T1) return false;
		}
		OutTNear = T0; return true;
	}

	struct FMeshView
	{
		const float*    Pos; const uint32_t* Idx; const uint32_t* TriOrder;
	};

	static inline void TestLeaf(const FMeshView& M, const FRay& R, uint32_t First, uint32_t Count, FHit& H, FTraversalStats& S)
	{
		for (uint32_t i = 0; i < Count; ++i)
		{
			const uint32_t Tri = M.TriOrder[First + i];
			const float* A = &M.Pos[M.Idx[Tri * 3 + 0] * 3];
			const float* B = &M.Pos[M.Idx[Tri * 3 + 1] * 3];
			const float* C = &M.Pos[M.Idx[Tri * 3 + 2] * 3];
			S.TriTests++;
			float T;
			if (RayTri(R, A, B, C, T) && T < H.T) { H.T = T; H.Tri = Tri; }
		}
	}

	static inline FHit TraceBinary(const FBvhBinary& B, const FMeshView& M, FRay R, FTraversalStats& S)
	{
		FHit H; if (B.Nodes.empty()) return H;
		float Inv[3] = { 1.f / (R.D[0] != 0.f ? R.D[0] : 1e-20f), 1.f / (R.D[1] != 0.f ? R.D[1] : 1e-20f), 1.f / (R.D[2] != 0.f ? R.D[2] : 1e-20f) };
		uint32_t Stack[64]; int SP = 0; Stack[SP++] = 0;
		while (SP)
		{
			const FBinaryNode& N = B.Nodes[Stack[--SP]];
			S.NodesVisited++;
			float Tn;
			if (!RayBox(R.O, Inv, N.Min, N.Max, H.T < R.TMax ? H.T : R.TMax, Tn)) continue;
			if (N.Count) { TestLeaf(M, R, N.LeftOrFirst, N.Count, H, S); continue; }
			Stack[SP++] = N.LeftOrFirst; Stack[SP++] = N.LeftOrFirst + 1;
		}
		return H;
	}

	template <typename TNodeBox>
	static inline FHit TraceWide(const std::vector<TNodeBox>& Nodes, const uint32_t* (*ChildPtr)(const TNodeBox&), void (*BoxOf)(const TNodeBox&, int, float*, float*),
		uint32_t RootRef, const FMeshView& M, FRay R, FTraversalStats& S)
	{
		FHit H;
		if (RefIsEmpty(RootRef)) return H;
		if (RefIsLeaf(RootRef)) { TestLeaf(M, R, RefLeafFirst(RootRef), RefLeafCount(RootRef), H, S); return H; }
		float Inv[3] = { 1.f / (R.D[0] != 0.f ? R.D[0] : 1e-20f), 1.f / (R.D[1] != 0.f ? R.D[1] : 1e-20f), 1.f / (R.D[2] != 0.f ? R.D[2] : 1e-20f) };
		struct E { uint32_t Ref; float T; };
		E Stack[128]; int SP = 0; Stack[SP++] = { RootRef, 0.f };
		while (SP)
		{
			const E Cur = Stack[--SP];
			if (Cur.T > H.T) continue;
			if (RefIsLeaf(Cur.Ref)) { TestLeaf(M, R, RefLeafFirst(Cur.Ref), RefLeafCount(Cur.Ref), H, S); continue; }
			const TNodeBox& N = Nodes[Cur.Ref];
			S.NodesVisited++;
			const uint32_t* Ch = ChildPtr(N);
			E Hits[4]; int NH = 0;
			for (int i = 0; i < 4; ++i)
			{
				if (RefIsEmpty(Ch[i])) continue;
				float Lo[3], Hi[3], Tn;
				BoxOf(N, i, Lo, Hi);
				if (RayBox(R.O, Inv, Lo, Hi, H.T < R.TMax ? H.T : R.TMax, Tn)) Hits[NH++] = { Ch[i], Tn };
			}
			// ordenar por T decrescente (o mais perto fica no topo da pilha)
			for (int i = 1; i < NH; ++i) { E K = Hits[i]; int j = i - 1; while (j >= 0 && Hits[j].T < K.T) { Hits[j + 1] = Hits[j]; --j; } Hits[j + 1] = K; }
			for (int i = 0; i < NH; ++i) Stack[SP++] = Hits[i];
		}
		return H;
	}

	static inline const uint32_t* ChildPtrU(const FBvh4Node& N) { return N.Child; }
	static inline void BoxOfU(const FBvh4Node& N, int i, float* Lo, float* Hi) { for (int a = 0; a < 3; ++a) { Lo[a] = N.Lo[i][a]; Hi[a] = N.Hi[i][a]; } }
	static inline const uint32_t* ChildPtrC(const FBvh4NodeCompressed& N) { return N.Child; }
	static inline void BoxOfC(const FBvh4NodeCompressed& N, int i, float* Lo, float* Hi) { FBvh4Compressor::DecodeBox(N, i, Lo, Hi); }

	static inline FHit TraceBvh4(const FBvh4& B, const FMeshView& M, const FRay& R, FTraversalStats& S) { return TraceWide<FBvh4Node>(B.Nodes, ChildPtrU, BoxOfU, B.RootRef, M, R, S); }
	static inline FHit TraceBvh4C(const FBvh4Compressed& B, const FMeshView& M, const FRay& R, FTraversalStats& S) { return TraceWide<FBvh4NodeCompressed>(B.Nodes, ChildPtrC, BoxOfC, B.RootRef, M, R, S); }
}
