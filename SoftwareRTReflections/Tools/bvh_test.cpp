// Teste standalone do builder de BVH (nao precisa do Unreal).
//   g++ -O2 -std=c++17 -I../Source/SoftwareRTReflections/Private/BVH bvh_test.cpp -o bvh_test && ./bvh_test
//
// Verifica:
//   1) Os tres formatos devolvem o mesmo hit que a forca bruta (mesmo T, dentro de tolerancia).
//   2) A compressao a 8 bits e CONSERVADORA (a caixa descomprimida contem sempre a original).
//   3) Todos os triangulos aparecem exatamente uma vez nas folhas.
// Imprime tamanhos e custo de travessia na CPU (nos visitados, testes de triangulo).
// NOTA: estes numeros sao de CPU, nao de GPU. Servem para comparar formatos entre si, nao para prever ms.
#include "SWRTBvh.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>

using namespace SWRT;

struct FScene
{
	std::vector<float>    Pos;
	std::vector<uint32_t> Idx;
	uint32_t NumTris() const { return (uint32_t)(Idx.size() / 3); }
};

// Cena com clusters (mais parecida com geometria real do que ruido uniforme).
static FScene MakeScene(uint32_t Tris, uint32_t Seed, bool bDegenerateCluster)
{
	std::mt19937 Rng(Seed);
	std::uniform_real_distribution<float> U(0.f, 1.f);
	std::normal_distribution<float> G(0.f, 1.f);
	FScene S;
	const int NumClusters = 40;
	std::vector<float> Centers(NumClusters * 4);
	for (int c = 0; c < NumClusters; ++c)
	{
		Centers[c * 4 + 0] = U(Rng) * 2000.f - 1000.f;
		Centers[c * 4 + 1] = U(Rng) * 2000.f - 1000.f;
		Centers[c * 4 + 2] = U(Rng) * 400.f;
		Centers[c * 4 + 3] = 20.f + U(Rng) * 150.f;
	}
	for (uint32_t t = 0; t < Tris; ++t)
	{
		const int c = (int)(U(Rng) * NumClusters) % NumClusters;
		float Base[3] = { Centers[c * 4] + G(Rng) * Centers[c * 4 + 3], Centers[c * 4 + 1] + G(Rng) * Centers[c * 4 + 3], Centers[c * 4 + 2] + G(Rng) * Centers[c * 4 + 3] };
		if (bDegenerateCluster && t < 40) { Base[0] = 5.f; Base[1] = 5.f; Base[2] = 5.f; } // 40 triangulos identicos
		for (int v = 0; v < 3; ++v)
		{
			const uint32_t Index = (uint32_t)(S.Pos.size() / 3);
			const float Size = (bDegenerateCluster && t < 40) ? 1.f : (2.f + U(Rng) * 14.f);
			S.Pos.push_back(Base[0] + (bDegenerateCluster && t < 40 ? (v == 1 ? 1.f : 0.f) : G(Rng) * Size));
			S.Pos.push_back(Base[1] + (bDegenerateCluster && t < 40 ? (v == 2 ? 1.f : 0.f) : G(Rng) * Size));
			S.Pos.push_back(Base[2] + (bDegenerateCluster && t < 40 ? 0.f : G(Rng) * Size));
			S.Idx.push_back(Index);
		}
	}
	return S;
}

// Transliteracao C++ do decode em SWRTBvhTraversal.ush (le o no como 16 palavras de 32 bit, como Load4/Load3 fariam).
static void HlslStyleDecode(const FBvh4NodeCompressed& N, int Child, float* Lo, float* Hi)
{
	uint32_t W[16]; std::memcpy(W, &N, 64);
	float P[3]; std::memcpy(P, &W[0], 12);
	const uint32_t ExpPacked = W[3];
	const uint32_t QL[3] = { W[8], W[9], W[10] };   // offset 32
	const uint32_t QH[3] = { W[11], W[12], W[13] }; // offset 44
	auto GetByte = [](const uint32_t* Wd, uint32_t I) { return (Wd[I >> 2] >> ((I & 3u) * 8u)) & 0xFFu; };
	for (int a = 0; a < 3; ++a)
	{
		uint32_t Bits = ((ExpPacked >> (8 * a)) & 0xFFu) << 23; float Scale; std::memcpy(&Scale, &Bits, 4);
		Lo[a] = P[a] + (float)GetByte(QL, Child * 3 + a) * Scale;
		Hi[a] = P[a] + (float)GetByte(QH, Child * 3 + a) * Scale;
	}
}

static void Normalize(float* D) { const float L = std::sqrt(D[0] * D[0] + D[1] * D[1] + D[2] * D[2]); for (int i = 0; i < 3; ++i) D[i] /= L; }

static void CountLeafTris(uint32_t Ref, const FBvh4& B, std::vector<int>& Seen, const FBvhBinary& Bin)
{
	if (RefIsEmpty(Ref)) return;
	if (RefIsLeaf(Ref)) { for (uint32_t i = 0; i < RefLeafCount(Ref); ++i) Seen[Bin.TriOrder[RefLeafFirst(Ref) + i]]++; return; }
	const FBvh4Node& N = B.Nodes[Ref];
	for (int i = 0; i < 4; ++i) CountLeafTris(N.Child[i], B, Seen, Bin);
}

static int RunCase(const char* Name, const FScene& S, uint32_t NumRays, uint32_t NumBruteRays)
{
	FBvhBuildSettings Settings;
	FBvhBinary Bin;
	auto T0 = std::chrono::high_resolution_clock::now();
	FBinaryBuilder(S.Pos.data(), S.Idx.data(), S.NumTris(), Settings).Build(Bin);
	auto T1 = std::chrono::high_resolution_clock::now();
	FBvh4 Wide; FBvh4Collapser::Collapse(Bin, Wide);
	FBvh4Compressed Comp; FBvh4Compressor::Compress(Wide, Comp);

	int Failures = 0;

	// 3) cada triangulo aparece exatamente uma vez
	{
		std::vector<int> Seen(S.NumTris(), 0);
		CountLeafTris(Wide.RootRef, Wide, Seen, Bin);
		for (int c : Seen) if (c != 1) { ++Failures; }
		if (Failures) std::printf("  [FALHA] cobertura de triangulos incorreta (%d)\n", Failures);
	}

	// 2) compressao conservadora
	int NonConservative = 0;
	for (size_t n = 0; n < Wide.Nodes.size(); ++n)
		for (int i = 0; i < 4; ++i)
		{
			if (RefIsEmpty(Wide.Nodes[n].Child[i])) continue;
			float Lo[3], Hi[3]; FBvh4Compressor::DecodeBox(Comp.Nodes[n], i, Lo, Hi);
			for (int a = 0; a < 3; ++a)
				if (Lo[a] > Wide.Nodes[n].Lo[i][a] + 1e-4f || Hi[a] < Wide.Nodes[n].Hi[i][a] - 1e-4f) ++NonConservative;
		}
	if (NonConservative) { std::printf("  [FALHA] %d caixas comprimidas NAO sao conservadoras\n", NonConservative); ++Failures; }

	// 2b) o decode "estilo HLSL" tem de ser identico ao decode do C++ (valida o layout de bytes)
	int HlslMismatch = 0;
	for (size_t n = 0; n < Comp.Nodes.size(); ++n)
		for (int i = 0; i < 4; ++i)
		{
			if (RefIsEmpty(Comp.Nodes[n].Child[i])) continue;
			float L1[3], H1[3], L2[3], H2[3];
			FBvh4Compressor::DecodeBox(Comp.Nodes[n], i, L1, H1);
			HlslStyleDecode(Comp.Nodes[n], i, L2, H2);
			for (int a = 0; a < 3; ++a) if (L1[a] != L2[a] || H1[a] != H2[a]) ++HlslMismatch;
		}
	if (HlslMismatch) { std::printf("  [FALHA] decode estilo HLSL difere em %d valores\n", HlslMismatch); ++Failures; }

	// rays (origem espalhada, direcao aleatoria; muitos acertam)
	std::mt19937 Rng(1234);
	std::uniform_real_distribution<float> U(-1.f, 1.f);
	std::vector<FRay> Rays(NumRays);
	for (auto& R : Rays)
	{
		R.O[0] = U(Rng) * 1200.f; R.O[1] = U(Rng) * 1200.f; R.O[2] = U(Rng) * 300.f + 100.f;
		R.D[0] = U(Rng); R.D[1] = U(Rng); R.D[2] = U(Rng) * 0.6f - 0.4f; Normalize(R.D);
		R.TMin = 1e-3f; R.TMax = 5000.f;
	}

	FMeshView M{ S.Pos.data(), S.Idx.data(), Bin.TriOrder.data() };
	FTraversalStats SB, S4, SC; int Hits = 0, Mismatch = 0;
	for (uint32_t r = 0; r < NumRays; ++r)
	{
		FHit hb = TraceBinary(Bin, M, Rays[r], SB);
		FHit h4 = TraceBvh4(Wide, M, Rays[r], S4);
		FHit hc = TraceBvh4C(Comp, M, Rays[r], SC);
		if (hb.Tri != 0xFFFFFFFFu) ++Hits;

		auto Same = [&](const FHit& A, const FHit& B) { if (A.Tri == 0xFFFFFFFFu || B.Tri == 0xFFFFFFFFu) return A.Tri == B.Tri; return std::fabs(A.T - B.T) <= 1e-3f * (1.f + A.T); };
		if (!Same(hb, h4) || !Same(hb, hc)) ++Mismatch;

		if (r < NumBruteRays)
		{
			FHit hf; FTraversalStats Dummy;
			FMeshView Id{ S.Pos.data(), S.Idx.data(), nullptr };
			for (uint32_t t = 0; t < S.NumTris(); ++t)
			{
				float T; if (RayTri(Rays[r], &S.Pos[S.Idx[t * 3] * 3], &S.Pos[S.Idx[t * 3 + 1] * 3], &S.Pos[S.Idx[t * 3 + 2] * 3], T) && T < hf.T) { hf.T = T; hf.Tri = t; }
			}
			(void)Id; (void)Dummy;
			if (!Same(hf, hb) || !Same(hf, h4) || !Same(hf, hc)) { ++Mismatch; if (Mismatch < 4) std::printf("  mismatch ray %u: brute T=%f tri=%u | bin T=%f | bvh4 T=%f | bvh4c T=%f\n", r, hf.T, hf.Tri, hb.T, h4.T, hc.T); }
		}
	}
	if (Mismatch) { std::printf("  [FALHA] %d rays com resultado diferente\n", Mismatch); ++Failures; }

	const double Bld = std::chrono::duration<double, std::milli>(T1 - T0).count();
	std::printf("\n== %s: %u tris, %u rays (%u vs forca bruta), %d acertos ==\n", Name, S.NumTris(), NumRays, NumBruteRays, Hits);
	std::printf("  build binario: %.1f ms\n", Bld);
	std::printf("  %-18s %10s %12s %14s %14s\n", "formato", "nos", "bytes", "nos/ray", "tris-testes/ray");
	std::printf("  %-18s %10zu %12zu %14.1f %14.1f\n", "binario",       Bin.Nodes.size(),  Bin.Nodes.size()  * sizeof(FBinaryNode),         (double)SB.NodesVisited / NumRays, (double)SB.TriTests / NumRays);
	std::printf("  %-18s %10zu %12zu %14.1f %14.1f\n", "BVH4",          Wide.Nodes.size(), Wide.Nodes.size() * sizeof(FBvh4Node),           (double)S4.NodesVisited / NumRays, (double)S4.TriTests / NumRays);
	std::printf("  %-18s %10zu %12zu %14.1f %14.1f\n", "BVH4 comprimido", Comp.Nodes.size(), Comp.Nodes.size() * sizeof(FBvh4NodeCompressed), (double)SC.NodesVisited / NumRays, (double)SC.TriTests / NumRays);
	std::printf("  resultado: %s\n", Failures ? "FALHOU" : "OK");
	return Failures;
}

int main()
{
	int F = 0;
	F += RunCase("pequena",              MakeScene(2000, 1, false),    4000, 1000);
	F += RunCase("media",                MakeScene(50000, 2, false),   20000, 300);
	F += RunCase("grande",               MakeScene(300000, 3, false),  20000, 40);
	F += RunCase("degenerada (40 iguais)", MakeScene(3000, 4, true),   4000, 1000);
	std::printf("\nTOTAL: %s\n", F ? "FALHAS" : "TUDO OK");
	return F ? 1 : 0;
}
