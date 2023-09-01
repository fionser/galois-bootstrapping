#pragma once
#include <vector>
#include <assert.h>
#include <unordered_map>
#include <iostream>
#include "seal/util/uintarithsmallmod.h"
#include "polyarith.h"
#include "slots.h"

template <class T>
constexpr inline std::size_t hash_combine(T const& v,
	std::size_t const seed = {}) noexcept
{
	return seed ^ (std::hash<T>()(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2));
}

template <typename... Args>
struct std::hash<std::tuple<Args...>> {
	std::size_t operator()(const std::tuple<Args...>& tuple) const noexcept
	{
		return std::apply([](const auto&... args) {
			auto seed = static_cast<std::size_t>(672807365);
			((seed = hash_combine(args, seed)), ...);
			return seed;
		}, tuple);
	}
};

/**
 * Precomputes all powers of a given element and stores them for fast access.
*/
class NegacyclicPowerTable {

	const SlotRing::SubringView& ring;
	uint64_t N;
	poly generator;
	std::vector<poly> content;

public:
	NegacyclicPowerTable(const SlotRing::SubringView& ring, poly generator, uint64_t half_order_generator);
	NegacyclicPowerTable(const NegacyclicPowerTable&) = default;
	NegacyclicPowerTable(NegacyclicPowerTable&&) = default;
	~NegacyclicPowerTable() = default;

	poly operator[](int64_t i) const;
};

class CompiledSubringLinearTransform;

//tex:
//Stores a linear transform $R_t \to R_t$ as
//$$R_t \to R_t, \quad x \mapsto \sum_i c_i \sigma_i(x)$$
//where $\sigma_i$ are the Galois automorphisms of $R = \mathbb{Z}[X]/(X^N + 1)$
class CompiledLinearTransform {

	std::shared_ptr<const SlotRing> slot_ring;

	//tex:
	//The coefficients $c_i$ of the transform $$x \mapsto \sum_i c_i \sigma_i(x)$$
	//The order of the $\sigma_i$ is as follows
	//$$ \sigma_i: X \to X^{g_1^{mi}} \quad \text{if $mi < \mathrm{ord}(g_1)$} $$
	//$$ \sigma_i: X \to X^{g_1^{mi} g_2} \quad \text{if $mi \geq \mathrm{ord}(g_1)$} $$
	//Here $m \ | \ \mathrm{ord}(g_1)$ is gives the order of the subgroup which to use.
	//Furthermore, it might be the case that we only have $\mathrm{ord}(g_1)/m$ coefficients,
	//in which case the second line is irrelevant (this is e.g. the case if $(\mathbb{Z}/2N\mathbb{Z})^*/\langle p \rangle$
	//is generated by $g_1$ alone).
	//
	//Note that instead of the $c_i$, we store automorphisms of the $c_i$ because of the baby-step-giant-step
	//approach.
	std::vector<poly> coefficients;

	//tex:
	//Most general constructor, directly takes the given values into the members.
	//Note that this does not compute
	//$$x \mapsto \sum_i \mathrm{coefficients}[i] \cdot \sigma_i(x)$$
	//as the coefficients will be pushed through the giant-step automorphisms.
	//If this is not wanted, consider calling fix_coefficient_shift() afterwards.
	CompiledLinearTransform(std::shared_ptr<const SlotRing> slot_ring, std::vector<poly> coefficients);

	//tex:
	//Computes the $i$-th automorphism, which is defined as
	//$$ \sigma_i: X \to X^{g_1^{mi}} \quad \text{if $mi < \mathrm{ord}(g_1)$} $$
	//$$ \sigma_i: X \to X^{g_1^{mi} g_2} \quad \text{if $mi \geq \mathrm{ord}(g_1)$} $$
	SlotRing::RawAuto automorphism(size_t index) const;

	//tex:
	//Computes the automorphism $\sigma_i^{-1} \circ \sigma_j$ where $i = \mathrm{from}$ and $j = \mathrm{to}$.
	//$\sigma_i$ here is defined as in the doc of automorphism().
	SlotRing::RawAuto difference_automorphism(size_t from, size_t to) const;

	SlotRing::RawAuto reverse_automorphism(size_t index) const;

	//tex:
	//The size of the subgroup $K \subseteq \mathbb{Z}/\mathrm{ord}(g_1)\mathbb{Z}$ of the automorphisms to use.
	//In more detail, the transform to compute is of the form
	//$$ \alpha \mapsto \sum_{k \in K} \sum_l a_{kl} \sigma_{g_1^kg_2^l}(\alpha)$$
	//Note that currently only the subgroup $K = \mathbb{Z}/\mathrm{ord}(g_1)\mathbb{Z}$ is fully supported.
	uint64_t g1_subgroup_order() const;

	//tex:
	//The size of the subgroup $L \subseteq \mathbb{Z}/\mathrm{ord}(g_2)\mathbb{Z}$ of the automorphisms to use.
	//In more detail, the transform to compute is of the form
	//$$ \alpha \mapsto \sum_k \sum_{l \in L} a_{kl} \sigma_{g_1^kg_2^l}(\alpha)$$
	//Note that $\mathrm{ord}(g_2) = 2$, so either $L = \{ 0 \}$ or $L = \{ 0, 1 \}$.
	uint64_t g2_subgroup_order() const;

	//tex:
	//Changes this linear transform $f$ to $f'$, where
	//$$f': x \mapsto f(x) + \mathrm{rot}(\mathrm{frob}(x)) \cdot c$$
	//for the scaling factor $c$. 
	void add_scaled_transform(const poly& scaling, const SlotRing::Rotation& rotation, const SlotRing::Frobenius& frobenius);

	size_t babystep_automorphism_count() const;
	size_t giantstep_automorphism_count() const;

	//tex:
	//The baby-step-giant-step approach to apply the transform has the side-effect that the giant-step
	//automorphisms are applied to the coefficients during evaluation. More concretely, during evaluation
	//we compute
	//$$ x \mapsto \sum_{j = 0}^{\sqrt{N} - 1} \sigma_{jN}\Bigl(\sum_{i = 0}^{\sqrt{N} - 1} c_i \sigma_i(x) \Bigr) $$
	//assuming that $N$ is a perfect square and the automorphism group is cyclic (i.e. $(\mathbb{Z}/2N\mathbb{Z})^*/\langle p \rangle$
	//is cyclic). If this is not the case, the computations will look more complicated.
	//In any case, the coefficient multiplied with $\sigma_k(x)$ is not $c_k$, but $\sigma_{j(k)}(c_k)$.
	//This function applies $\sigma_{j(k)}^{-1}$ to each coefficient, thus "undoing" this "coefficient shift".
	void fix_coefficient_shift();

	//tex:
	//Asume that the given ring $R = (\mathbb{Z}/p^e\mathbb{Z})[X]/(f)$ is generated by a primitive $2N$-th root of unity $\zeta$, whose powers
	//are stored in the powertable. Further, we assume that $f$ is irreducible modulo $p$. Assume taht the the given matrix represents the linear transform
	//$f: R \to R$ w.r.t. the basis given by the powers of $\zeta$.
	//
	//Then this function returns $d = \mathrm{deg}(f)$ coefficients such that $f$ is given by
	//$$f: x \mapsto \sum_{i = 0}^{d - 1} c_i \pi^i(x)$$
	//where $\pi^i$ is the $p^i$-th power Frobenius.
	static std::vector<poly> compile_frobenius(
		const std::unordered_map<std::tuple<size_t, size_t>, uint64_t>& sparse_transform_matrix,
		const SlotRing::SubringView& ring,
		uint64_t p,
		size_t N,
		const NegacyclicPowerTable& powertable
	);

public:

	CompiledLinearTransform(const CompiledLinearTransform&) = default;
	CompiledLinearTransform(CompiledLinearTransform&&) = default;
	~CompiledLinearTransform() = default;

	//tex:
	//Computes the linear transform given by the input matrix w.r.t. the slot basis.
	//
	//In more detail, consider the basis of $R/p^eR$ given by
	//$$e_0, Xe_0, ..., X^{d - 1}e_0, \ e_1, X^{g_1}e_1, X^{2g_1}e_1, ..., X^{dg_1}e_1, \ e_2, X^{g_1^2}e_2, X^{2g_1^2}e_2, ..., \ ...$$
	//where $e_i$ is the $i$-th slot unit vector.
	//Now given a matrix $A \in (\mathbb{Z}/p^e\mathbb{Z})^{N \times N}$, we can consider the linear transform
	//$R/p^eR \to R/p^eR$ given by $A$. This transform is computed by the function, where the matrix $A$ is passed
	//as a function that, on input $i$, $j$, fills the given map with the entries of the $(i, j)$-th $d \times d$ block of $A$.
	//$$~$$
	//As a note on the use_g2 parameter: In general, it would be nice to allow for more efficient computation in the case that
	//the used automorphisms only correspond to a subgroup of $\mathrm{Gal}(R/\mathbb{Z})$. However, since the $g_1$-dimension is
	//"bad", this does not correspond to using a subgroup of the rotations group, and hence is hard to use (in particular does not
	//work with the evaluation map). Instead, we just provide the option to set use_g2 = false, which performs the transform using
	//only the automorphisms $\sigma_{g_1^k}$, but not $\sigma_{g_2}$.
	template<typename T>
	static CompiledLinearTransform compile_slot_basis(std::shared_ptr<const SlotRing> slot_ring, T sparse_transform_matrix_per_slot, bool use_g2 = true);

	static CompiledLinearTransform scalar_slots_to_first_coefficients(std::shared_ptr<const SlotRing> slot_ring);
	static CompiledLinearTransform first_coefficients_to_scalar_slots(std::shared_ptr<const SlotRing> slot_ring);

	static CompiledLinearTransform load_binary(std::shared_ptr<const SlotRing> slot_ring, std::istream& in);
	void save_binary(std::ostream& stream) const;

	CompiledSubringLinearTransform in_ring() &&;

	friend class CompiledSubringLinearTransform;
};

//tex:
//Represents a linear transform that predictably acts only on a subring.
//
//Assume $R = (\mathbb{Z}/t\mathbb{Z})[x] = (\mathbb{Z}/t\mathbb{X})[X]/(X^N + 1)$. 
//We consider the ring $R$ and its subring $S = (Z/tZ)[x^2]$ of index 2. Now we could 
//have a linear transform of $S$ and want to apply it to a value in $S$, but encrypted as a 
//value of $R$. This can be done with this class.
//$$~$$
//Note that this only provides a performance benefit, as (of course) a linear
//transform of $S$ can be extended to a linear transform of $R$. However, the extended
//transform then requires computing more automorphisms than necessary.
//
//In other words, the main idea is that if we assume the input to be in $S$, some
//automorphisms (the cosets of $\mathrm{Gal}(R/(\mathbb{Z}/t\mathbb{Z})) / \mathrm{Gal}(R/S)$) act identically on it.
class CompiledSubringLinearTransform {

	CompiledLinearTransform subring_transform;
	std::shared_ptr<const SlotRing> slot_ring;
	mutable std::vector<seal::Plaintext> coefficients_plain = {};

	SlotRing::RawAuto automorphism(size_t index) const;
	SlotRing::RawAuto difference_automorphism(size_t from, size_t to) const;
	SlotRing::RawAuto reverse_automorphism(size_t index) const;
	size_t babystep_automorphism_count() const;
	size_t giantstep_automorphism_count() const;

public:
	CompiledSubringLinearTransform(CompiledLinearTransform&& transform, std::shared_ptr<const SlotRing> new_ring);
	CompiledSubringLinearTransform(const CompiledSubringLinearTransform&) = default;
	CompiledSubringLinearTransform(CompiledSubringLinearTransform&&) = default;
	~CompiledSubringLinearTransform() = default;

	static CompiledSubringLinearTransform slots_to_coeffs(std::shared_ptr<const SlotRing> slot_ring);
	static CompiledSubringLinearTransform coeffs_to_slots(std::shared_ptr<const SlotRing> slot_ring);

	poly operator()(const poly& x) const;
	void apply_ciphertext(const seal::Ciphertext& in, const seal::Evaluator& eval, const seal::GaloisKeys& gk, seal::Ciphertext& result) const;

	//tex:
	//Returns a set of elements of $(\mathbb{Z}/2N\mathbb{Z})^*$ such that the corresponding Galois automorphisms
	//suffice to compute this transform.
	std::vector<uint32_t> galois_elements() const;

	CompiledLinearTransform&& transform() &&;
};

void test_first_coeffs_to_scalar_slots();
void test_compile_slot_basis();
void test_apply_ciphertext();
void test_apply_ciphertext_subring();

template<typename T>
inline CompiledLinearTransform CompiledLinearTransform::compile_slot_basis(std::shared_ptr<const SlotRing> slot_ring, T sparse_transform_matrix_per_slot, bool use_g2)
{
	size_t block_size = slot_ring->slot_group_len();
	if (!use_g2) {
		block_size = block_size / 2;
	}
	std::vector<SlotRing::Rotation> rotations;
	for (size_t i = 0; i < block_size; ++i) {
		rotations.push_back(slot_ring->block_rotate(i, block_size));
	}
	std::unique_ptr<SlotRing::Rotation> lane_switch;
	if (!use_g2) {
		lane_switch = std::make_unique<SlotRing::Rotation>(slot_ring->rotate(block_size));
	}

	const size_t d = slot_ring->slot_rank();
	std::unordered_map<std::tuple<size_t, size_t>, uint64_t> slotwise_matrix;
	NegacyclicPowerTable powertable(slot_ring->R(), slot_ring->from_slot_value({ 0, 1 }, 0), slot_ring->N());

	std::vector<poly> coefficients;
	coefficients.resize(block_size * d);
	for (auto& c : coefficients) {
		c.resize(slot_ring->N());
	}
	CompiledLinearTransform result(slot_ring, std::move(coefficients));

	for (size_t s = 0; s < block_size; ++s) {
		for (size_t j = 0; j < block_size; ++j) {
			const size_t block_row = j;
			const size_t block_col = (j + block_size - s) % block_size;
			slotwise_matrix.clear();
			sparse_transform_matrix_per_slot(slotwise_matrix, block_row, block_col);
			if (slotwise_matrix.size() == 0) {
				continue;
			}
			std::vector<poly> frobenius_form = CompiledLinearTransform::compile_frobenius(
				slotwise_matrix,
				slot_ring->slot(),
				slot_ring->prime(),
				slot_ring->N(),
				powertable
			);
			for (size_t l = 0; l < d; ++l) {
				poly coeff = rotations[j](frobenius_form[l]);
				result.add_scaled_transform(
					coeff,
					rotations[s],
					slot_ring->frobenius(l)
				);
				if (!use_g2) {
					coeff = (*lane_switch)(coeff);
					result.add_scaled_transform(
						coeff,
						rotations[s],
						slot_ring->frobenius(l)
					);
				}
			}
		}
	}
	result.fix_coefficient_shift();
	return result;
}
